/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include "tcp-dsdcc.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDsdcc");

NS_OBJECT_ENSURE_REGISTERED (TcpDsdcc);

TypeId TcpDsdcc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDsdcc")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpDsdcc> ()
    .SetGroupName ("Internet")
    .AddAttribute ("DsdccShiftG",
                   "Parameter G for updating dsdcc_alpha",
                   DoubleValue (0.0625),
                   MakeDoubleAccessor (&TcpDsdcc::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DsdccAlphaOnInit",
                   "Initial alpha value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDsdcc::InitializeDsdccAlpha),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("UseEct0",
                   "Use ECT(0) for ECN codepoint, if false use ECT(1)",
                   BooleanValue (true),
                   MakeBooleanAccessor (&TcpDsdcc::m_useEct0),
                   MakeBooleanChecker ())
    .AddAttribute ("DsdccNqK",
                   "Network queue threshold calculated by rtt",
                   UintegerValue (5),
                   MakeUintegerAccessor (&TcpDsdcc::m_nq_k),
                    MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpDsdcc::m_traceCongestionEstimate),
                     "ns3::TcpDsdcc::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpDsdcc::GetName () const
{
  return "TcpDsdcc";
}

TcpDsdcc::TcpDsdcc ()
  : TcpLinuxReno (),
    m_ackedBytesEcn (0),
    m_ackedBytesTotal (0),
    m_priorRcvNxt (SequenceNumber32 (0)),
    m_priorRcvNxtFlag (false),
    m_nextSeq (SequenceNumber32 (0)),
    m_nextSeqFlag (false),
    m_ceState (false),
    m_delayedAckReserved (false),
    m_initialized (false)
{
  NS_LOG_FUNCTION (this);
}

TcpDsdcc::TcpDsdcc (const TcpDsdcc& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesEcn (sock.m_ackedBytesEcn),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_alpha_ecn (sock.m_alpha_ecn),
    m_alpha_rtt (sock.m_alpha_rtt),
    m_alpha (sock.m_alpha),
    m_nextSeq (sock.m_nextSeq),
    m_nextSeqFlag (sock.m_nextSeqFlag),
    m_ceState (sock.m_ceState),
    m_delayedAckReserved (sock.m_delayedAckReserved),
    m_g (sock.m_g),
    m_useEct0 (sock.m_useEct0),
    m_initialized (sock.m_initialized)
{
  NS_LOG_FUNCTION (this);
}

TcpDsdcc::~TcpDsdcc (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpDsdcc::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpDsdcc> (this);
}

void
TcpDsdcc::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Enabling DctcpEcn for DSDCC");
  tcb->m_useEcn = TcpSocketState::On;
  tcb->m_ecnMode = TcpSocketState::DctcpEcn;
  tcb->m_ectCodePoint = m_useEct0 ? TcpSocketState::Ect0 : TcpSocketState::Ect1;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDsdcc::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return static_cast<uint32_t> ((1 - m_alpha / 2.0) * tcb->m_cWnd);
}

void
TcpDsdcc::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);
  m_ackedBytesTotal += segmentsAcked * tcb->m_segmentSize;

  // calculate network queue and avg network queue
  if (!rtt.IsZero())
    {
      /* Calculate the difference between the expected rate (segCwnd/base_rtt) and the actual rate (segCwnd/current_rtt), 
       * the diff * base_rtt is the extra data in the network -- network queue
      */
      uint32_t segCwnd = tcb->GetCwndInSegments();
      int64_t current_rtt = rtt.GetMicroSeconds();
      int64_t base_rtt = tcb->m_minRtt.GetMicroSeconds();
      // current_rtt should be bigger than base_rtt
      NS_ASSERT(current_rtt >= base_rtt);
      int32_t nq = segCwnd * (current_rtt-base_rtt) / current_rtt;
      NS_LOG_DEBUG("segCwnd: " << segCwnd << ", current rtt: " << current_rtt << ", base rtt: " << base_rtt << ", nq: " << nq);
      
      if (nq >= m_nq_k)
        {
          m_signal = Signal::rtt_sig;
          m_ackedBytesRtt += segmentsAcked * tcb->m_segmentSize;
        }
    }


  if (tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD)
    {
      m_ackedBytesEcn += segmentsAcked * tcb->m_segmentSize;
    }
  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }
    //long flow 
  if(tcb->m_flowMode == TcpSocketState::Elephant){
    // rtt expire, do this once in each RTT
    if (tcb->m_lastAckedSeq >= m_nextSeq)
      {
        double bytesRtt = 0.0;
        if (m_ackedBytesTotal >  0)
          {
            bytesRtt = static_cast<double> (m_ackedBytesRtt * 1.0 / m_ackedBytesTotal);
          }

        m_alpha_rtt = (1.0 - m_g) * m_alpha_rtt + m_g * bytesRtt;
        m_traceCongestionEstimate (m_ackedBytesRtt, m_ackedBytesTotal, m_alpha_rtt);
        NS_LOG_INFO (this << "bytesEcn " << bytesRtt << ", m_alpha " << m_alpha_rtt);
        m_alpha = m_alpha_rtt;
        //reduce cwnd
        if(m_signal == Signal::rtt_sig)
          {
            uint32_t val = static_cast<uint32_t> ((1 - m_alpha_rtt / 2.0) * tcb->m_cWnd);
            tcb->m_ssThresh = std::max (val, 2 * tcb->m_segmentSize);
            tcb->m_cWnd = tcb->m_ssThresh;
          }
        Reset (tcb);
      }
  }
  //short flow
  else if(tcb->m_flowMode == TcpSocketState::Mouse){
    if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesEcn = 0.0; // Corresponds to variable M in RFC 8257
      if (m_ackedBytesTotal >  0)
        {
          bytesEcn = static_cast<double> (m_ackedBytesEcn * 1.0 / m_ackedBytesTotal);
        }
      m_alpha_ecn = (1.0 - m_g) * m_alpha_ecn + m_g * bytesEcn;
      m_alpha = m_alpha_ecn;
      m_traceCongestionEstimate (m_ackedBytesEcn, m_ackedBytesTotal, m_alpha_ecn);
      NS_LOG_INFO (this << "bytesEcn " << bytesEcn << ", m_alpha " << m_alpha_ecn);
      Reset (tcb);
    }
  }
}

void
TcpDsdcc::InitializeDsdccAlpha (double alpha)
{
  NS_LOG_FUNCTION (this << alpha);
  NS_ABORT_MSG_IF (m_initialized, "DSDCC has already been initialized");
  m_alpha_ecn = alpha;
  m_alpha_rtt = alpha;
  m_alpha = alpha;
}

void
TcpDsdcc::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesEcn = 0;
  m_ackedBytesRtt = 0;
  m_ackedBytesTotal = 0;
  m_signal = TcpDsdcc::non_sig;
}

void
TcpDsdcc::CeState0to1 (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (!m_ceState && m_delayedAckReserved && m_priorRcvNxtFlag)
    {
      SequenceNumber32 tmpRcvNxt;
      /* Save current NextRxSequence. */
      tmpRcvNxt = tcb->m_rxBuffer->NextRxSequence ();

      /* Generate previous ACK without ECE */
      tcb->m_rxBuffer->SetNextRxSequence (m_priorRcvNxt);
      tcb->m_sendEmptyPacketCallback (TcpHeader::ACK);

      /* Recover current RcvNxt. */
      tcb->m_rxBuffer->SetNextRxSequence (tmpRcvNxt);
    }

  if (m_priorRcvNxtFlag == false)
    {
      m_priorRcvNxtFlag = true;
    }
  m_priorRcvNxt = tcb->m_rxBuffer->NextRxSequence ();
  m_ceState = true;
  tcb->m_ecnState = TcpSocketState::ECN_CE_RCVD;
}

void
TcpDsdcc::CeState1to0 (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (m_ceState && m_delayedAckReserved && m_priorRcvNxtFlag)
    {
      SequenceNumber32 tmpRcvNxt;
      /* Save current NextRxSequence. */
      tmpRcvNxt = tcb->m_rxBuffer->NextRxSequence ();

      /* Generate previous ACK with ECE */
      tcb->m_rxBuffer->SetNextRxSequence (m_priorRcvNxt);
      tcb->m_sendEmptyPacketCallback (TcpHeader::ACK | TcpHeader::ECE);

      /* Recover current RcvNxt. */
      tcb->m_rxBuffer->SetNextRxSequence (tmpRcvNxt);
    }

  if (m_priorRcvNxtFlag == false)
    {
      m_priorRcvNxtFlag = true;
    }
  m_priorRcvNxt = tcb->m_rxBuffer->NextRxSequence ();
  m_ceState = false;

  if (tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
    {
      tcb->m_ecnState = TcpSocketState::ECN_IDLE;
    }
}

void
TcpDsdcc::UpdateAckReserved (Ptr<TcpSocketState> tcb,
                             const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  switch (event)
    {
    case TcpSocketState::CA_EVENT_DELAYED_ACK:
      if (!m_delayedAckReserved)
        {
          m_delayedAckReserved = true;
        }
      break;
    case TcpSocketState::CA_EVENT_NON_DELAYED_ACK:
      if (m_delayedAckReserved)
        {
          m_delayedAckReserved = false;
        }
      break;
    default:
      /* Don't care for the rest. */
      break;
    }
}

void
TcpDsdcc::CwndEvent (Ptr<TcpSocketState> tcb,
                     const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  switch (event)
    {
    case TcpSocketState::CA_EVENT_ECN_IS_CE:
      CeState0to1 (tcb);
      break;
    case TcpSocketState::CA_EVENT_ECN_NO_CE:
      CeState1to0 (tcb);
      break;
    case TcpSocketState::CA_EVENT_DELAYED_ACK:
    case TcpSocketState::CA_EVENT_NON_DELAYED_ACK:
      UpdateAckReserved (tcb, event);
      break;
    default:
      /* Don't care for the rest. */
      break;
    }
}

} // namespace ns3
