#include "lra-helper.h"
#include "lra-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "ns3/ptr.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LraHelper");

LraHelper::~LraHelper ()
{
  NS_LOG_FUNCTION (this);
}

LraHelper::LraHelper () : Ipv4RoutingHelper ()
{
  NS_LOG_FUNCTION (this);
  m_agentFactory.SetTypeId ("ns3::LraRoutingProtocol");
}

LraHelper* LraHelper::Copy (void) const
{
  NS_LOG_FUNCTION (this);
  return new LraHelper (*this);
}

Ptr<Ipv4RoutingProtocol> LraHelper::Create (Ptr<Node> node) const
{
  NS_LOG_FUNCTION (this << node);
  Ptr<LraRoutingProtocol>
  agent = m_agentFactory.Create<LraRoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void LraHelper::Set (std::string name, const AttributeValue &value)
{
  NS_LOG_FUNCTION (this << name);
  m_agentFactory.Set (name, value);
}

int64_t
LraHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;
    Ptr<Node> node;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        node = (*i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");
        Ptr<LraRoutingProtocol> lra = DynamicCast<LraRoutingProtocol>(proto);
        if (lra)
        {
            currentStream += lra->AssignStreams(currentStream);
            continue;
        }
        // Lra may also be in a list
        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            Ptr<Ipv4RoutingProtocol> listProto;
            Ptr<LraRoutingProtocol> listLra;
            for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
            {
                listProto = list->GetRoutingProtocol(i, priority);
                listLra = DynamicCast<LraRoutingProtocol>(listProto);
                if (listLra)
                {
                    currentStream += listLra->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }
    return (currentStream - stream);
}

} //end namespace ns3