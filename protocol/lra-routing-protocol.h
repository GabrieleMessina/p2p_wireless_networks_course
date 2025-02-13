#ifndef LRA_ROUTING_PROTOCOL_H
#define LRA_ROUTING_PROTOCOL_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/node.h"
#include "ns3/ipv4-address.h"
#include "ns3/header.h"
#include "ns3/nstime.h"
#include "algorithm"
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <chrono>

namespace ns3 {

enum RecvLraStatus{
  Service,
  NotService,
  Error
};

class LraRoutingProtocol : public Ipv4RoutingProtocol {
public:
  static TypeId GetTypeId (void);
  static const uint32_t LRA_PORT;
  static const char * LRA_ACK_SEND_MESSAGE;
  static const char * LRA_ACK_RECV_MESSAGE;
  static const char * LRA_HELLO_RECV_MESSAGE;
  static const char * LRA_HELLO_SEND_MESSAGE;
  static const char * LRA_REVERSAL_SEND_MESSAGE;

  /// c-tor
  LraRoutingProtocol ();
  /** Dummy destructor, see DoDispose. */
  virtual  ~LraRoutingProtocol ();
  /** Destructor implementation */
  virtual void  DoDispose ();
  // Inherited methods:
  Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                              Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  bool RouteInput (
    Ptr<const Packet> p, 
    const Ipv4Header &header,
    Ptr<const NetDevice> idev, 
    const UnicastForwardCallback &ucb,
    const MulticastForwardCallback &mcb,
    const LocalDeliverCallback &lcb, 
    const ErrorCallback &ecb);
  virtual void PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;
  virtual void NotifyInterfaceUp (uint32_t interface);
  virtual void NotifyInterfaceDown (uint32_t interface);
  virtual void NotifyAddAddress (uint32_t interface,
                                 Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress (uint32_t interface,
                                    Ipv4InterfaceAddress address);

  virtual void SetIpv4(Ptr<Ipv4> ipv4);
  // Custom methods:
  void InitializeNode(Ipv4Address sinkAddress, int index);
  float GetAverageHopCount();
  int64_t AssignStreams(int64_t stream);

private:
  void LinkReversal();
  void SendHelloMessage (Ipv4Address destination);
  void SendAckRequestMessage (Ipv4Address destination);
  void SendHelloResponseMessage (Ipv4Address origin);
  void SendAckResponseMessage (Ipv4Address origin);
  void SendServiceMessagePacket(Ipv4Address destination, const char* type);
  void SendReversalMessage (Ipv4Address destination);
  RecvLraStatus RecvLraServiceMessage(std::string payload, Ipv4Address origin);
  void DisableLinkTo(Ipv4Address destination, bool avoidReverse = false);
  void EnableLinkTo(Ipv4Address destination);
  void InitLinkTo(Ipv4Address destination);
  Ipv4Address GetNextHop();
  Ipv4Address _GetNextHop();
  bool HasNextHop();
  std::string GetPacketPayload(Ptr<const Packet> p);

  Ipv4Address m_sink; // Destination
  Ipv4Address m_nodeAddress; // Node Address
  Ipv4Address m_broadcastAddress; // Broadcast Address
  bool initialized = false;
  uint m_index; // Index of node based on creation
  float hopSum; // Sum of hop count (for average calculation)
  int nPacketReceived; // Number of received packets (for hop count average calculation)
  Ptr<Ipv4> m_ipv4;
  std::set<Ipv4Address> m_neighbors; // Direct neighbors
  std::map<Ipv4Address, int> m_linkStatus; // Link orientation (1 = active/exiting)
  std::map<Ipv4Address, uint> m_cycleDetection; // Keep trace of cycle for each neighbor
  std::map<Ipv4Address, EventId> m_disableLinkToEvent; // Event that fires link disable when neighbor is not reachable
};
} // namespace ns3

#endif // LRA_ROUTING_PROTOCOL_H
