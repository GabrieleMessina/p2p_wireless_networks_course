#ifndef LRA_HELPER_H
#define LRA_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-routing-helper.h"


namespace ns3 {

class LraHelper : public Ipv4RoutingHelper
{
public:
  /** Constructor */
  LraHelper ();
  /** Destructor */
  ~LraHelper ();
  // Inherited
  LraHelper* Copy (void) const;
  virtual Ptr<Ipv4RoutingProtocol> Create (Ptr<Node> node) const;
  /**
   * Set attributes by name.
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set.
   *
   * This method controls the attributes of "ns3::Lra::RoutingProtocol"
   */
  void Set (std::string name, const AttributeValue &value);

  int64_t AssignStreams(NodeContainer c, int64_t stream);

private:
  /** The factory to create Lra routing object */
  ObjectFactory m_agentFactory;
};

} //end namespace ns3

#endif /* LRA_HELPER_H */