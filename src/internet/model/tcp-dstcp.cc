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

#include "tcp-dstcp.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDstcp");

NS_OBJECT_ENSURE_REGISTERED (TcpDstcp);

TypeId TcpDstcp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDstcp")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpDstcp> ()
    .SetGroupName ("Internet")
    .AddAttribute ("DstcpShiftG",
                   "Parameter G for updating dstcp_alpha",
                   DoubleValue (0.0625),
                   MakeDoubleAccessor (&TcpDstcp::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("AvgNqShiftG",
                   "Parameter G for updating avg network queue",
                   DoubleValue (0.875),
                   MakeDoubleAccessor (&TcpDstcp::m_nq_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DstcpNqK1",
                   "Network queue threshold 1 calculated by rtt",
                   UintegerValue (5),
                   MakeUintegerAccessor (&TcpDstcp::m_nq_k1),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DstcpNqK2",
                   "Network queue threshold 2 calculated by rtt",
                   UintegerValue (20),
                   MakeUintegerAccessor (&TcpDstcp::m_nq_k2),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DstcpAlphaOnInit",
                   "Initial alpha value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDstcp::InitializeDstcpAlpha),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DrainCycle",
                   "dstcp drain cycle",
                   UintegerValue (8),
                   MakeUintegerAccessor (&TcpDstcp::m_drain_cycle),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DrainCycleScale",
                   "dstcp cycle scale",
                   UintegerValue (1),
                   MakeUintegerAccessor (&TcpDstcp::m_drain_cycle_scale),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DrainCwnd",
                   "dstcp drain cwnd",
                   DoubleValue (5.5),
                   MakeDoubleAccessor (&TcpDstcp::m_drain_cwnd),
                   MakeDoubleChecker<double> (0, 100))
    .AddAttribute ("DrainCwndScale",
                   "the drain cwnd scale in sig_drain",
                   UintegerValue (1),
                   MakeUintegerAccessor (&TcpDstcp::m_drain_cwnd_scale),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("UseEct0",
                   "Use ECT(0) for ECN codepoint, if false use ECT(1)",
                   BooleanValue (true),
                   MakeBooleanAccessor (&TcpDstcp::m_useEct0),
                   MakeBooleanChecker ())
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpDstcp::m_traceCongestionEstimate),
                     "ns3::TcpDstcp::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpDstcp::GetName () const
{
  return "TcpDstcp";
}

TcpDstcp::TcpDstcp ()
  : TcpLinuxReno (),
    m_ackedBytesEcn (0),
    m_ackedBytesRtt (0),
    m_ackedBytesTotal (0),
    m_nq_avg (-1),
    m_priorRcvNxt (SequenceNumber32 (0)),
    m_priorRcvNxtFlag (false),
    m_signal (non_sig),
    m_last_minRtt (INT64_MAX),
    m_nextSeq (SequenceNumber32 (0)),
    m_nextSeqFlag (false),
    m_ceState (false),
    m_delayedAckReserved (false),
    m_initialized (false),
    m_drain_cycle (8),
    // m_last_drain_cwnd (UINT32_MAX),
    m_drain_cycle_scale (1),
    m_drain_cwnd_scale (1),
    m_round (0)
{
  NS_LOG_FUNCTION (this);
}

TcpDstcp::TcpDstcp (const TcpDstcp& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesEcn (sock.m_ackedBytesEcn),
    m_ackedBytesRtt (sock.m_ackedBytesRtt),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_nq_k1 (sock.m_nq_k1),
    m_nq_k2 (sock.m_nq_k2),
    m_nq_avg (sock.m_nq_avg),
    m_nq_g (sock.m_nq_g),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_alpha_ecn (sock.m_alpha_ecn),
    m_alpha_rtt (sock.m_alpha_rtt),
    m_alpha (sock.m_alpha),
    m_signal (sock.m_signal),
    m_last_minRtt (sock.m_last_minRtt),
    m_nextSeq (sock.m_nextSeq),
    m_nextSeqFlag (sock.m_nextSeqFlag),
    m_ceState (sock.m_ceState),
    m_delayedAckReserved (sock.m_delayedAckReserved),
    m_g (sock.m_g),
    m_useEct0 (sock.m_useEct0),
    m_initialized (sock.m_initialized),
    m_drain_cycle (sock.m_drain_cycle),
    // m_last_drain_cwnd (sock.m_last_drain_cwnd),
    m_drain_cycle_scale (sock.m_drain_cycle_scale),
    m_drain_cwnd_scale (sock.m_drain_cwnd_scale),
    m_round (sock.m_round)
{
  NS_LOG_FUNCTION (this);
}

TcpDstcp::~TcpDstcp (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpDstcp::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpDstcp> (this);
}

void
TcpDstcp::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Enabling DctcpEcn for DSTCP");
  tcb->m_useEcn = TcpSocketState::On;
  tcb->m_ecnMode = TcpSocketState::DctcpEcn;
  tcb->m_ectCodePoint = m_useEct0 ? TcpSocketState::Ect0 : TcpSocketState::Ect1;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDstcp::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return static_cast<uint32_t> ((1 - m_alpha / 2.0) * tcb->m_cWnd);
}

void
TcpDstcp::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
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
      m_last_minRtt = std::min(m_last_minRtt, current_rtt);
      // current_rtt should be bigger than base_rtt
      NS_ASSERT(current_rtt >= base_rtt);
      int32_t nq = segCwnd * (current_rtt-base_rtt) / current_rtt;
      NS_LOG_DEBUG("segCwnd: " << segCwnd << ", current rtt: " << current_rtt << ", base rtt: " << base_rtt << ", nq: " << nq);
      
      if (nq >= m_nq_k1)
        {
          m_ackedBytesRtt += segmentsAcked * tcb->m_segmentSize;
          m_signal = Signal::rtt_sig;
        }

      if (m_nq_avg < 0)
        {
          m_nq_avg = nq;
        }
      else 
        {
          // m_nq_avg <—— (1-m_nq_g) * m_nq_avg + m_nq_g * nq, calculate the avg network queue in this rtt window
          m_nq_avg = m_nq_avg + m_nq_g * (nq - m_nq_avg);
        }
    }

  if (tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD)
    {
      m_ackedBytesEcn += segmentsAcked * tcb->m_segmentSize;
      m_signal = TcpDstcp::ecn_sig;
    }
  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }
  
  // rtt expire, do this once in each RTT
  if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesEcn = 0.0; // Corresponds to variable M in RFC 8257
      double bytesRtt = 0.0;
      if (m_ackedBytesTotal >  0)
        {
          bytesEcn = static_cast<double> (m_ackedBytesEcn * 1.0 / m_ackedBytesTotal);
          bytesRtt = static_cast<double> (m_ackedBytesRtt * 1.0 / m_ackedBytesTotal);
        }
      m_alpha_ecn = (1.0 - m_g) * m_alpha_ecn + m_g * bytesEcn;
      m_alpha_rtt = (1.0 - m_g) * m_alpha_rtt + m_g * bytesRtt;

      // NS_LOG_DEBUG("avg_nq: " << m_nq_avg);
      if (m_signal == TcpDstcp::ecn_sig && m_nq_avg >= m_nq_k2) 
        {
          m_signal = TcpDstcp::ear_sig;
        }

      m_round += 1;
      // std::cout<< "cycle: " << m_cycle << std::endl;
      if( ( m_round % (m_drain_cycle * m_drain_cycle_scale) ) == 0 ){
        if(tcb->m_cWnd > static_cast<uint32_t>(m_drain_cwnd * m_drain_cwnd_scale * tcb->m_segmentSize))
          {
            m_signal = TcpDstcp::drain_sig;
          }
        // if(tcb->m_cWnd > m_last_drain_cwnd){
        //   m_cycle = std::min(m_cycle+1, static_cast<uint32_t>(32));
        // }else{
        //   m_cycle = std::max(m_cycle-2, static_cast<uint32_t>(8));
        // }
        // m_last_drain_cwnd = static_cast<uint32_t>(tcb->m_cWnd);
        m_round = 0;
      }
      // NS_LOG_DEBUG("mode: " << m_signal);
      // std::cout<< "signal: " << m_signal << std::endl;
      switch (m_signal)
        {
        // 快速收敛
        case TcpDstcp::drain_sig:
          {
            tcb->m_cWnd = static_cast<uint32_t>(m_drain_cwnd * m_drain_cwnd_scale * tcb->m_segmentSize);//5.5
            tcb->m_ssThresh = tcb->m_cWnd;
            tcb->m_cWndInfl = tcb->m_cWnd;
          }
          break;
        case TcpDstcp::non_sig:
          break;
        case TcpDstcp::ecn_sig:
          {
            m_alpha = m_alpha_ecn;
          }
          break;
        case TcpDstcp::rtt_sig:
          {
            // tcp fairness，公平收敛
            tcb->m_cWnd = tcb->m_cWnd - (1-m_alpha_rtt)*tcb->m_segmentSize;
            tcb->m_ssThresh = tcb->m_cWnd;
            tcb->m_cWndInfl = tcb->m_cWnd;
          }
          break;
        case TcpDstcp::ear_sig:
          {
            m_alpha = m_alpha_ecn+m_alpha_rtt;
          }
          break;
        default:
          /* Don't care for the rest. */
          break;
        }

      // m_alpha = (1.0 - m_g) * m_alpha + m_g * bytesEcn;
      m_traceCongestionEstimate (m_ackedBytesEcn, m_ackedBytesTotal, m_alpha);
      NS_LOG_INFO (this << "bytesEcn " << bytesEcn << ", m_alpha " << m_alpha);
      Reset (tcb);
    }
}

void
TcpDstcp::InitializeDstcpAlpha (double alpha)
{
  NS_LOG_FUNCTION (this << alpha);
  NS_ABORT_MSG_IF (m_initialized, "DSTCP has already been initialized");
  m_alpha_ecn = alpha;
  m_alpha_rtt = alpha;
  m_alpha = alpha;
}

void
TcpDstcp::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesEcn = 0;
  m_ackedBytesRtt = 0;
  m_ackedBytesTotal = 0;
  m_signal = TcpDstcp::non_sig;
  m_nq_avg = -1;
  m_last_minRtt = INT64_MAX;
}

void
TcpDstcp::CeState0to1 (Ptr<TcpSocketState> tcb)
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
TcpDstcp::CeState1to0 (Ptr<TcpSocketState> tcb)
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
TcpDstcp::UpdateAckReserved (Ptr<TcpSocketState> tcb,
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
TcpDstcp::CwndEvent (Ptr<TcpSocketState> tcb,
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
