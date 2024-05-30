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

#ifndef TCP_DCVEGAS_H
#define TCP_DCVEGAS_H

#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-linux-reno.h"
#include "ns3/traced-callback.h"

namespace ns3 {

/**
 * \ingroup tcp
 *
 * \brief An implementation of DCVEGAS.
 */

class TcpDcvegas : public TcpLinuxReno
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * Create an unbound tcp socket.
   */
  TcpDcvegas ();

  /**
   * \brief Copy constructor
   * \param sock the object to copy
   */
  TcpDcvegas (const TcpDcvegas& sock);

  /**
   * \brief Destructor
   */
  virtual ~TcpDcvegas (void);

  // Documented in base class
  virtual std::string GetName () const;

  /**
   * \brief Set configuration required by congestion control algorithm
   *
   * \param tcb internal congestion state
   */
  virtual void Init (Ptr<TcpSocketState> tcb);

  /**
   * TracedCallback signature for Dcvegas update of congestion state
   *
   * \param [in] bytesAcked Bytes acked in this observation window
   * \param [in] bytesMarked Bytes marked in this observation window
   * \param [in] alpha New alpha (congestion estimate) value
   */
  typedef void (* CongestionEstimateTracedCallback)(uint32_t bytesAcked, uint32_t bytesMarked, double alpha);

  // Documented in base class
  virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb,
                                uint32_t bytesInFlight);
  virtual Ptr<TcpCongestionOps> Fork ();
  virtual void PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                          const Time &rtt);
private:

  /**
   * \brief Resets the value of m_ackedBytesRtt, m_ackedBytesTotal and m_nextSeq
   *
   * \param tcb internal congestion state
   */
  void Reset (Ptr<TcpSocketState> tcb);

  /**
   * \brief Initialize the value of m_alpha
   *
   * \param alpha Dcvegas alpha parameter
   */
  void InitializeDcvegasAlpha (double alpha);

  uint32_t m_ackedBytesRtt;             //!< Number of acked bytes which are marked by RTT
  uint32_t m_ackedBytesTotal;           //!< Total number of acked bytes

  int32_t m_nq_k;                     //!< Network queue threshold calculated by rtt
 
  SequenceNumber32 m_priorRcvNxt;       //!< Sequence number of the first missing byte in data
  bool m_priorRcvNxtFlag;               //!< Variable used in setting the value of m_priorRcvNxt for first time
 
  double m_alpha;                   //!< Parameter used to estimate the amount of network congestion calculated by rtt
 
  typedef enum{
      non_sig,
      rtt_sig
  } Signal;

  Signal m_signal;

  SequenceNumber32 m_nextSeq;           //!< TCP sequence number threshold for beginning a new observation window
  bool m_nextSeqFlag;                   //!< Variable used in setting the value of m_nextSeq for first time
    
  double m_g;                           //!< Estimation gain
  bool m_initialized;                   //!< Whether DCVEGAS has been initialized
  /**
   * \brief Callback pointer for congestion state update
   */
  TracedCallback<uint32_t, uint32_t, double> m_traceCongestionEstimate;
};

} // namespace ns3

#endif /* TCP_DCVEGAS_H */

