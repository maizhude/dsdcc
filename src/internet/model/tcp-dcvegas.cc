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

#include "tcp-dcvegas.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDcvegas");

NS_OBJECT_ENSURE_REGISTERED (TcpDcvegas);

TypeId TcpDcvegas::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDcvegas")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpDcvegas> ()
    .SetGroupName ("Internet")
    .AddAttribute ("DcvegasShiftG",
                   "Parameter G for updating dcvegas_alpha",
                   DoubleValue (0.0625),
                   MakeDoubleAccessor (&TcpDcvegas::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DcvegasNqK",
                   "Network queue threshold calculated by rtt",
                   UintegerValue (5),
                   MakeUintegerAccessor (&TcpDcvegas::m_nq_k),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DcvegasAlphaOnInit",
                   "Initial alpha value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDcvegas::InitializeDcvegasAlpha),
                   MakeDoubleChecker<double> (0, 1))
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpDcvegas::m_traceCongestionEstimate),
                     "ns3::TcpDcvegas::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpDcvegas::GetName () const
{
  return "TcpDcvegas";
}

TcpDcvegas::TcpDcvegas ()
  : TcpLinuxReno (),
    m_ackedBytesRtt (0),
    m_ackedBytesTotal (0),
    m_priorRcvNxt (SequenceNumber32 (0)),
    m_priorRcvNxtFlag (false),
    m_signal (non_sig),
    m_nextSeq (SequenceNumber32 (0)),
    m_nextSeqFlag (false),
    m_initialized (false)
{
  NS_LOG_FUNCTION (this);
}

TcpDcvegas::TcpDcvegas (const TcpDcvegas& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesRtt (sock.m_ackedBytesRtt),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_nq_k (sock.m_nq_k),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_alpha (sock.m_alpha),
    m_signal (sock.m_signal),
    m_nextSeq (sock.m_nextSeq),
    m_nextSeqFlag (sock.m_nextSeqFlag),
    m_g (sock.m_g),
    m_initialized (sock.m_initialized)
{
  NS_LOG_FUNCTION (this);
}

TcpDcvegas::~TcpDcvegas (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpDcvegas::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpDcvegas> (this);
}

void
TcpDcvegas::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Init TcpDcvegas");
  tcb->m_useEcn = TcpSocketState::Off;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDcvegas::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return static_cast<uint32_t> ((1 - m_alpha / 2.0) * tcb->m_cWnd);
}

void
TcpDcvegas::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
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
      // NS_LOG_DEBUG("segCwnd: " << segCwnd << ", current rtt: " << current_rtt << ", base rtt: " << base_rtt << ", nq: " << nq);
      
      if (nq >= m_nq_k)
        {
          m_signal = Signal::rtt_sig;
          m_ackedBytesRtt += segmentsAcked * tcb->m_segmentSize;
        }
    }

  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }
  
  // rtt expire, do this once in each RTT
  if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesRtt = 0.0;
      if (m_ackedBytesTotal >  0)
        {
          bytesRtt = static_cast<double> (m_ackedBytesRtt * 1.0 / m_ackedBytesTotal);
        }

      m_alpha = (1.0 - m_g) * m_alpha + m_g * bytesRtt;
      m_traceCongestionEstimate (m_ackedBytesRtt, m_ackedBytesTotal, m_alpha);
      NS_LOG_INFO (this << "bytesEcn " << bytesRtt << ", m_alpha " << m_alpha);
      // reduce cwnd
      if(m_signal == TcpDcvegas::rtt_sig)
        {
          uint32_t val = static_cast<uint32_t> ((1 - m_alpha / 2.0) * tcb->m_cWnd);
          tcb->m_ssThresh = std::max (val, 2 * tcb->m_segmentSize);
          tcb->m_cWnd = tcb->m_ssThresh;
        }
      Reset (tcb);
    }
}

void
TcpDcvegas::InitializeDcvegasAlpha (double alpha)
{
  NS_LOG_FUNCTION (this << alpha);
  NS_ABORT_MSG_IF (m_initialized, "Dcvegas has already been initialized");
  m_alpha = alpha;
}

void
TcpDcvegas::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesRtt = 0;
  m_ackedBytesTotal = 0;
  m_signal = TcpDcvegas::non_sig;
}
} // namespace ns3
