#include "lra-routing-protocol.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"

#include <ranges>

using namespace std;

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LraRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(LraRoutingProtocol);
const uint32_t LraRoutingProtocol::LRA_PORT = 654;
const char* LraRoutingProtocol::LRA_ACK_SEND_MESSAGE = "LRA_ACK_SEND_MESSAGE";
const char* LraRoutingProtocol::LRA_ACK_RECV_MESSAGE = "LRA_ACK_RECV_MESSAGE";
const char* LraRoutingProtocol::LRA_HELLO_RECV_MESSAGE = "LRA_HELLO_RECV_MESSAGE";
const char* LraRoutingProtocol::LRA_HELLO_SEND_MESSAGE = "LRA_HELLO_SEND_MESSAGE";
const char* LraRoutingProtocol::LRA_REVERSAL_SEND_MESSAGE = "LRA_REVERSAL_SEND_MESSAGE";

TypeId
LraRoutingProtocol::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::LraRoutingProtocol")
                            .SetParent<Ipv4RoutingProtocol>()
                            .AddConstructor<LraRoutingProtocol>();
    return tid;
}

LraRoutingProtocol::LraRoutingProtocol()
{
    NS_LOG_FUNCTION(this);
    hopSum = 0;
    nPacketReceived = 0;
}

LraRoutingProtocol::~LraRoutingProtocol()
{
    NS_LOG_FUNCTION(this);
}

void
LraRoutingProtocol::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Ipv4RoutingProtocol::DoDispose();
}

Ptr<Ipv4Route>
LraRoutingProtocol::RouteOutput(Ptr<Packet> packet,
                                const Ipv4Header& header,
                                Ptr<NetDevice> oif,
                                Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << header << packet->GetUid());

    Ipv4Address dest = header.GetDestination();

    // Packet arrived to destination
    if (dest == m_nodeAddress)
    {
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetSource(m_nodeAddress);
        route->SetDestination(dest);
        route->SetGateway(dest);
        route->SetOutputDevice(m_ipv4->GetNetDevice(1)); // Assume interface 1
        NS_LOG_INFO("Packet local delivery " << m_nodeAddress << " to " << dest);
        return route;
    }

    auto neighbor = (dest == m_sink)
                        ? GetNextHop()
                        : dest; // if dest is not sink then this is a service packet, all service
                                // packet have ttl=1 and must be forwarded directly to destination.
    if (neighbor != m_broadcastAddress)
    {
        // Create route forwarding packet to next hop
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetSource(m_nodeAddress);
        route->SetDestination(dest);
        route->SetGateway(neighbor);                     // Next hop
        route->SetOutputDevice(m_ipv4->GetNetDevice(1)); // Assume interface 1

        NS_LOG_INFO("Packet send from " << m_nodeAddress << " to " << dest << " through "
                                        << neighbor);
        return route;
    }

    // No route found
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return nullptr;
}

bool
LraRoutingProtocol::RouteInput(Ptr<const Packet> p,
                               const Ipv4Header& header,
                               Ptr<const NetDevice> idev,
                               const UnicastForwardCallback& ucb,
                               const MulticastForwardCallback& mcb,
                               const LocalDeliverCallback& lcb,
                               const ErrorCallback& ecb)
{
    if (!initialized)
    {
        return false;
    }

    std::string payload = GetPacketPayload(p);
    NS_LOG_FUNCTION(this << header << p->GetUid() << payload);
    int32_t iif = 1;
    Ipv4Address dest = header.GetDestination();
    Ipv4Address origin = header.GetSource();
    uint8_t ttl = header.GetTtl();
    uint8_t ttlMax = 64;

    if(ttl <= 0) return false;

    // Packet arrived to destination
    if (dest == m_nodeAddress || dest == m_broadcastAddress)
    {
        // Check if service message
        auto status = RecvLraServiceMessage(payload, origin);
        if(status == RecvLraStatus::Error) return false;
        else if(status == RecvLraStatus::NotService)
        {
            NS_LOG_INFO("Packet delivered to " << m_nodeAddress << " from " << origin);
            hopSum += float(static_cast<int>(ttlMax) - static_cast<int>(ttl));
            nPacketReceived++;
        }
        lcb(p, header, iif);
        return true;
    }

    if (dest == m_sink)
    {
        auto neighbor = GetNextHop();
        if (neighbor != m_broadcastAddress)
        {
            // Create route forwarding packet to next hop
            Ptr<Ipv4Route> route = Create<Ipv4Route>();
            route->SetSource(origin);
            route->SetDestination(dest);
            route->SetGateway(neighbor);
            route->SetOutputDevice(m_ipv4->GetNetDevice(1));
            NS_LOG_INFO("Packet forwarded from " << m_nodeAddress << " to " << neighbor << " for "
                                                 << dest << " and source " << origin);

            ucb(route, p, header);

            SendAckRequestMessage(neighbor);
            return true;
        }
    }

    // No route found
    NS_LOG_INFO("No route found for packet.");
    ecb(p, header, Socket::ERROR_NOROUTETOHOST);
    return false;
}

void
LraRoutingProtocol::InitializeNode(Ipv4Address sinkAddress, int index)
{
    NS_LOG_FUNCTION(this << sinkAddress << index);

    m_sink = sinkAddress;
    m_index = index;

    int randDelay = rand() % 1000;
    Time jitter = Time(MilliSeconds((double)index * 1000.0L + randDelay));
    if (m_nodeAddress == m_sink)
        jitter = Time(MilliSeconds(1));
    Simulator::Schedule(jitter, &LraRoutingProtocol::SendHelloMessage, this, m_broadcastAddress);

    NS_LOG_INFO("Node " << m_nodeAddress << " initialized with sink address " << m_sink << " and "
                        << m_neighbors.size() << " neighbors.");
}

void
LraRoutingProtocol::DisableLinkTo(Ipv4Address destination, bool avoidReverse)
{
    NS_LOG_FUNCTION(this << destination);
    NS_LOG_INFO("Node " << m_nodeAddress << " disables link to " << destination);

    m_neighbors.insert(destination);

    if (m_linkStatus[destination] != 0)
    {
        m_linkStatus[destination] = 0;
    }

    m_disableLinkToEvent.erase(destination); // erase events linked to this ip address

    if (m_nodeAddress != m_sink)
    {
        if (!HasNextHop() && !avoidReverse)
        {
            LinkReversal();
            // Notify all nodes that you are now in active state.
            SendReversalMessage(m_broadcastAddress);
        }

        if (!HasNextHop())
        { // link reversal caused cascading reversals in other nodes that bringed this node to have
          // no outgoing route again. So this is an unconnected component to the sink.
            initialized = false;
        }
    }
}

void
LraRoutingProtocol::EnableLinkTo(Ipv4Address destination)
{
    NS_LOG_FUNCTION(this << destination);
    NS_LOG_INFO("Node " << m_nodeAddress << " enables link to " << destination);

    m_neighbors.insert(destination);

    if (m_linkStatus[destination] != 1)
    {
        m_linkStatus[destination] = 1;
    }

    m_disableLinkToEvent.erase(destination); // erase events linked to this ip address
}

void
LraRoutingProtocol::InitLinkTo(Ipv4Address destination)
{
    NS_LOG_FUNCTION(this << destination);
    NS_LOG_INFO("Node " << m_nodeAddress << " init link to " << destination);

    m_neighbors.insert(destination);
    m_linkStatus[destination] = -1;
    m_disableLinkToEvent.erase(destination); // erase events linked to this ip address
}

void
LraRoutingProtocol::LinkReversal()
{
    NS_LOG_FUNCTION(this);

    // Recursion base case
    if (m_nodeAddress == m_sink)
    {
        return;
    }

    // Actual inversion
    for (auto neighbor : m_neighbors)
    {
        if (neighbor != m_broadcastAddress)
        {
            m_linkStatus[neighbor] = 1;
        }
    }
}

void
LraRoutingProtocol::SendAckRequestMessage(Ipv4Address destination)
{
    NS_LOG_FUNCTION(this << destination << m_nodeAddress);

    // Send ack request if no other ack requst were send to this dest
    if (m_disableLinkToEvent.find(destination) == m_disableLinkToEvent.end())
    {
        SendServiceMessagePacket(destination, LraRoutingProtocol::LRA_ACK_SEND_MESSAGE);

        Time jitter = Time(MilliSeconds(100));
        auto scheduledEventId = Simulator::Schedule(jitter,
                                                    &LraRoutingProtocol::DisableLinkTo,
                                                    this,
                                                    destination,
                                                    false);
        m_disableLinkToEvent[destination] = scheduledEventId;

        NS_LOG_INFO("Ack Packet request send from " << m_nodeAddress << " to " << destination);
    }
}

void
LraRoutingProtocol::SendAckResponseMessage(Ipv4Address origin)
{
    SendServiceMessagePacket(origin, LraRoutingProtocol::LRA_ACK_RECV_MESSAGE);
    NS_LOG_INFO("ACK Packet response send to " << m_nodeAddress << " from " << origin);
}

void
LraRoutingProtocol::SendHelloMessage(Ipv4Address destination)
{
    NS_LOG_FUNCTION(this << destination << m_nodeAddress);
    NS_LOG_INFO("SendHelloMessage " << m_nodeAddress << " " << destination);

    SendServiceMessagePacket(destination, LraRoutingProtocol::LRA_HELLO_SEND_MESSAGE);

    initialized = true;
}

void
LraRoutingProtocol::SendHelloResponseMessage(Ipv4Address origin)
{
    NS_LOG_FUNCTION(this << origin);
    NS_LOG_INFO("SendHelloResponseMessage " << m_nodeAddress << " " << origin);

    SendServiceMessagePacket(origin, LraRoutingProtocol::LRA_HELLO_RECV_MESSAGE);
}

void
LraRoutingProtocol::SendReversalMessage(Ipv4Address destination)
{
    NS_LOG_FUNCTION(this << destination << m_nodeAddress);
    NS_LOG_INFO("SendReversalMessage " << m_nodeAddress << " " << destination);

    SendServiceMessagePacket(destination, LraRoutingProtocol::LRA_REVERSAL_SEND_MESSAGE);
}

void
LraRoutingProtocol::SendServiceMessagePacket(Ipv4Address destination, const char* type)
{
    NS_LOG_FUNCTION(this << destination << type);

    Ptr<Ipv4Route> ackRoute = Create<Ipv4Route>();
    ackRoute->SetSource(m_nodeAddress);
    ackRoute->SetDestination(destination);
    ackRoute->SetGateway(destination);                  // Next hop
    ackRoute->SetOutputDevice(m_ipv4->GetNetDevice(1)); // Assume interface 1

    std::ostringstream oss;
    oss << m_nodeAddress;
    std::string addrStr = oss.str();

    uint32_t textSize = std::strlen(type); // Include null terminator
    Ptr<Packet> ackPacket = Create<Packet>((uint8_t*)type, textSize);
    SocketIpTtlTag tag;
    uint8_t ttl = 1;
    tag.SetTtl(ttl);
    ackPacket->AddPacketTag(tag);
    m_ipv4->Send(ackPacket, m_nodeAddress, destination, Ipv4L3Protocol::PROT_NUMBER, ackRoute);
}

Ipv4Address
LraRoutingProtocol::GetNextHop()
{
    auto nextHop = _GetNextHop();
    if (nextHop != m_broadcastAddress)
    {
        return nextHop;
    }
    else
    {
        if (nextHop == m_broadcastAddress && m_neighbors.size() > 0)
        {
            LinkReversal();
            auto nextHop = _GetNextHop();
            // Notify all nodes that you are now in active state.
            SendReversalMessage(m_broadcastAddress);
            return nextHop;
        }
    }

    return m_broadcastAddress;
}

Ipv4Address
LraRoutingProtocol::_GetNextHop()
{
    if (m_nodeAddress == m_sink)
    {
        return m_broadcastAddress;
    }

    // To speed up routing is always better to deliver the packet with higher ip address.
    for (auto iter = m_neighbors.rbegin(); iter != m_neighbors.rend(); ++iter)
    {
        auto neighbor = *iter;
        if (neighbor == m_broadcastAddress)
            continue;
        if (m_linkStatus[neighbor] == 1 || m_nodeAddress == m_sink)
        {
            if (m_cycleDetection[neighbor] < 3)
            { // Check if nodes makes a connected component with no route to the sink
                NS_LOG_FUNCTION(this << neighbor);
                return neighbor;
            }
            else
            {
                continue; // check next neighbor
            }
        }
        if (m_linkStatus[neighbor] == -1)
        {
            EnableLinkTo(neighbor);
            NS_LOG_FUNCTION(this << neighbor);
            return neighbor;
        }
    }
    NS_LOG_FUNCTION(this << m_broadcastAddress);

    return m_broadcastAddress; // fallback address
}

bool
LraRoutingProtocol::HasNextHop()
{
    return _GetNextHop() != m_broadcastAddress; // fallback address
}

RecvLraStatus
LraRoutingProtocol::RecvLraServiceMessage(std::string payload, Ipv4Address origin)
{
    // Ack request received
    if (payload == std::string(LraRoutingProtocol::LRA_ACK_SEND_MESSAGE))
    {
        NS_LOG_INFO("ACK Packet request delivered to " << m_nodeAddress << " from " << origin);
        DisableLinkTo(origin);
        if (m_linkStatus[origin] == 1)
        {
            NS_LOG_INFO("Cycle between " << m_nodeAddress << " from " << origin);
            m_cycleDetection[origin]++;
            return RecvLraStatus::Error;
        }
        SendAckResponseMessage(origin);
    }
    // Ack response received
    else if (payload == std::string(LraRoutingProtocol::LRA_ACK_RECV_MESSAGE))
    {
        NS_LOG_INFO("ACK Packet response delivered to " << m_nodeAddress << " from " << origin);
        if (m_disableLinkToEvent.find(origin) != m_disableLinkToEvent.end())
        {
            m_disableLinkToEvent[origin].Cancel(); // link is active, avoid to disable the link.
            m_disableLinkToEvent.erase(origin);
        }
        EnableLinkTo(origin);
    }
    // Hello message received
    else if (payload == std::string(LraRoutingProtocol::LRA_HELLO_SEND_MESSAGE))
    {
        NS_LOG_INFO("Hello Packet delivered to " << m_nodeAddress << " from " << origin);
        if (m_nodeAddress < origin)
        {
            EnableLinkTo(origin);
        }
        else
        {
            DisableLinkTo(origin, true);
        }

        int randJitter = rand() % 1000;
        Time jitter = Time(MilliSeconds(randJitter));
        Simulator::Schedule(jitter,
                            &LraRoutingProtocol::SendHelloResponseMessage,
                            this,
                            origin);
    }
    // Hello message response received
    else if (payload == std::string(LraRoutingProtocol::LRA_HELLO_RECV_MESSAGE))
    {
        NS_LOG_INFO("Hello Packet response delivered to " << m_nodeAddress << " from "
                                                            << origin);
        if (m_nodeAddress < origin)
        {
            EnableLinkTo(origin);
        }
        else
        {
            DisableLinkTo(origin, true);
        }
    }
    // Set passive response received
    else if (payload == std::string(LraRoutingProtocol::LRA_REVERSAL_SEND_MESSAGE))
    {
        DisableLinkTo(origin);
    }
    else{
        return RecvLraStatus::NotService;
    }
    return RecvLraStatus::Service;
}

void
LraRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    NS_LOG_FUNCTION(this);
    for (auto neighbor : m_neighbors)
    {
        *stream->GetStream() << m_nodeAddress << "\t" << neighbor << "\t" << m_linkStatus.at(neighbor)
               << std::endl;
    }
}

std::string
LraRoutingProtocol::GetPacketPayload(Ptr<const Packet> p)
{
    // Read packet data
    uint32_t packetSize = p->GetSize();
    uint8_t* buffer = new uint8_t[packetSize];
    p->CopyData(buffer, packetSize);
    std::string payload = std::string((char*)buffer, packetSize);
    delete[] buffer;
    return payload;
}

float 
LraRoutingProtocol::GetAverageHopCount(){
    if(nPacketReceived == 0) return 0.0f;
    return hopSum / (float)nPacketReceived;
}

void
LraRoutingProtocol::NotifyInterfaceUp(uint32_t i)
{
    NS_LOG_FUNCTION(this << i);
    NS_LOG_INFO("NotifyInterfaceUp " << i);

    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
    Ipv4InterfaceAddress iface = l3->GetAddress(1, 0);
    m_broadcastAddress = iface.GetBroadcast();
    m_nodeAddress = iface.GetLocal();
}

void
LraRoutingProtocol::NotifyInterfaceDown(uint32_t i)
{
    NS_LOG_FUNCTION(this << i);
    NS_LOG_INFO("NotifyInterfaceDown " << i);
}

void
LraRoutingProtocol::NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << i << address);
    NS_LOG_INFO("NotifyAddAddress " << i << " address:" << address);
}

void
LraRoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << i << address);
    NS_LOG_INFO("NotifyRemoveAddress " << i << " address:" << address);
}

int64_t
LraRoutingProtocol::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    return 1;
}

void
LraRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_LOG_FUNCTION(this << ipv4);
    m_ipv4 = ipv4;
}
} // namespace ns3
