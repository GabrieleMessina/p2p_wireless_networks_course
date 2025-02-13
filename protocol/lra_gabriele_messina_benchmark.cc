#include "lra-helper.h"
#include "lra-routing-protocol.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/gnuplot-helper.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/ping-helper.h"
#include "ns3/ping.h"
#include "ns3/point-to-point-module.h"
#include "ns3/stats-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("GabrieleMessina");

int startDelay = 86400;

using namespace ns3;

bool isFileEmpty(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate);  // Open file in read mode, move to end
    return file.tellg() == 0;  // Check if file size is 0
}

auto start = std::chrono::high_resolution_clock::now();
void PrintCheckpoint(){
    auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
    std::cout<<"init end (took:" << elapsed.count()<< " seconds), starting echo.\n";
}

class LraExample
{
  public:
    LraExample();
    /**
     * \brief Configure script parameters
     * \param argc is the command line argument count
     * \param argv is the command line arguments
     * \return true on successful configuration
     */
    bool Configure(int argc, char** argv);
    /// Run simulation
    void Run();
    /**
     * Report results highlight
     * \param os the output stream
     */
    void Report(std::ostream& os);
    /**
     * Write full results to stream
     * \param stream the output stream
     */
    void SaveResult(std::ostream& stream);

  private:
    // parameters
    /// Number of nodes
    uint32_t size;
    /// Area side length
    double step;
    /// Number of packets
    uint32_t n_packets;
    /// Simulation time, seconds
    double totalTime;
    /// Write per-device PCAP traces if true
    bool pcap;
    /// Print routes if true
    bool printRoutes;
    /// Simulation start time in real clock
    std::chrono::time_point<std::chrono::high_resolution_clock> simulationStartTime;

    // network
    /// Address of the sink node
    Ipv4Address m_sinkAddress;
    /// all nodes used in the example
    NodeContainer nodes;
    /// devices used in the example
    NetDeviceContainer netDevices;
    /// interfaces used in the example
    Ipv4InterfaceContainer ipv4Interfaces;

    /// atomic accumulator to keep record of total packages count
    std::atomic_int tot_acnt{0};
    /// map to keep record of package loss per address (node)
    std::map<Ipv4Address, std::atomic_int> m_packetsSentByNodes;
  private:
    /// Create the nodes
    void CreateNodes();
    /// Create the devices
    void CreateDevices();
    /// Create the network
    void InstallInternetStack();
    /// Create the simulation applications
    void InstallApplications();
    /// Init LRA nodes
    void InitNodesRouting();
    /// Make nodes move around
    void OnInitializeComplete();

    /// Tracing methods and utils
    void LogMessageResponse(std::string ctx,
                            Ptr<const Packet> packet,
                            const Address& srcAddress,
                            const Address& destAddress);
    void LogMessageSend(std::string ctx,
                        Ptr<const Packet> packet,
                        const Address& srcAddress,
                        const Address& destAddress);
    uint32_t GetNodeIdFromContext(std::string ctx);
    Ipv4Address GetNodeAddressFromId(uint32_t id);
    uint32_t GetApplicationIndexFromContext(std::string ctx);
};

int
main(int argc, char** argv)
{
    // LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_ALL);
    // LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_ALL);
    // LogComponentEnable ("LraRoutingProtocol", LOG_LEVEL_INFO);
    std::string csvFileName = "./gnuplot/data_results.csv";

    start = std::chrono::high_resolution_clock::now();

    LraExample test;
    if (!test.Configure(argc, argv))
    {
        NS_FATAL_ERROR("Configuration failed. Aborted.");
    }

    test.Run();
    test.Report(std::cout);

    // Calculate real execution time
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Simulation time: " << elapsed.count() << " seconds."
                << std::endl;

    std::ofstream file(csvFileName, std::ios::app);
    if (!file) {
        std::cerr << "Error opening file!" << std::endl;
    }
    else{
        bool empty = isFileEmpty(csvFileName);
        if (empty) {
            file << "n_nodes,area_side,packets_per_node,tot_packets,n_package_loss,loss_percentage,averageHop,simulation_time,real_elapsed_time\n";
        }
        test.SaveResult(file);
        file.close();
    }
    return 0;
}

LraExample::LraExample()
    : size(10),
      step(10),
      n_packets(3),
      totalTime(10),
      pcap(false),
      printRoutes(true)
{
}

bool
LraExample::Configure(int argc, char** argv)
{
    SeedManager::SetSeed(12345);
    CommandLine cmd(__FILE__);

    cmd.AddValue("pcap", "Write PCAP traces.", pcap);
    cmd.AddValue("printRoutes", "Print routing table dumps.", printRoutes);
    cmd.AddValue("size", "Number of nodes.", size);
    cmd.AddValue("npackets", "Number packets.", n_packets);
    cmd.AddValue("time", "Simulation time, s.", totalTime);
    cmd.AddValue("side", "Simulation Area side length, m", step);

    cmd.Parse(argc, argv);
    return true;
}

void
LraExample::Run()
{
    simulationStartTime = std::chrono::high_resolution_clock::now();
    totalTime = 4000000000;

    Time jitter = Time(Seconds(startDelay));
    Simulator::Schedule(jitter, &LraExample::OnInitializeComplete, this);

    CreateNodes();
    CreateDevices();
    InstallInternetStack();
    InstallApplications();

    Config::DisconnectWithoutContext(
        "/NodeList/*/ApplicationList/*/$ns3::UdpEchoServer/RxWithAddresses",
        MakeCallback(&LraExample::LogMessageResponse, this));
    Config::DisconnectWithoutContext(
        "/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/TxWithAddresses",
        MakeCallback(&LraExample::LogMessageSend, this));

    if (printRoutes)
    {
        Ptr<OutputStreamWrapper> routingStream =
            Create<OutputStreamWrapper>("lra.final.routes", std::ios::out);
        Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(totalTime), routingStream);
    }

    std::cout << "Starting simulation for " << totalTime << " s ...\n";

    Simulator::Stop(Seconds(totalTime));

    Simulator::Run();

    Simulator::Destroy();
    Names::Clear();
}

void
LraExample::SaveResult(std::ostream& stream)
{
    int total_loss = 0;
    for (const auto& pair : m_packetsSentByNodes)
    {
        total_loss += pair.second;
    }
    auto loss = ((double)total_loss / tot_acnt) * 100.0;

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - simulationStartTime;

    Ptr<Node> node = nodes.Get(nodes.GetN()-1);
    Ptr<LraRoutingProtocol> lraRouting = node->GetObject<LraRoutingProtocol>();
    auto averageHop = lraRouting->GetAverageHopCount();

    stream<<size<<",";
    stream<<step<<",";
    stream<<n_packets<<",";
    stream<<tot_acnt<<",";
    stream<<total_loss<<",";
    stream<<loss<<",";
    stream<<averageHop<<",";
    stream<<totalTime<<",";
    stream<<elapsed.count();
    stream<<std::endl;
}

void
LraExample::Report(std::ostream&)
{
    std::cout << "Packages lost:" << std::endl;

    int total_loss = 0;
    for (const auto& pair : m_packetsSentByNodes)
    {
        std::cout << "Node Ip: " << pair.first << ", Packets Lost: " << pair.second << std::endl;
        total_loss += pair.second;
    }

    std::cout << "Total packets:" << tot_acnt << ", Total packets lost: " << total_loss
              << ", Loss(%): " << ((double)total_loss / tot_acnt) * 100.0 << std::endl;
}

void
LraExample::CreateNodes()
{
    uint32_t tot_nodes = size;
    uint32_t rectSize = step;
    std::cout << "Creating " << (unsigned)tot_nodes << " nodes in a " << rectSize * rectSize
              << " m^2 area.\n";
    nodes.Create(tot_nodes);

    for (uint32_t i = 0; i < tot_nodes; ++i)
    {
        // Name nodes
        std::ostringstream os;
        os << "node-" << i;
        Names::Add(os.str(), nodes.Get(i));
    }

    MobilityHelper mobility;
    mobility.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(rectSize) + ".0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max="+ std::to_string(rectSize) + ".0]"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Mode",
                              StringValue("Time"),
                              "Time",
                              StringValue(std::to_string(totalTime) + "s"),
                              "Speed",
                              StringValue("ns3::UniformRandomVariable[Min=0.0|Max=0.0]"),
                              "Bounds",
                              RectangleValue(Rectangle(0, rectSize, 0, rectSize)));
    mobility.Install(nodes);

    // Set mobility random number streams to fixed values
    mobility.AssignStreams(nodes, 12345); // set randomness
    AsciiTraceHelper ascii;
    MobilityHelper::EnableAsciiAll(ascii.CreateFileStream("mobility-trace-lra.mob"));
}

void LraExample::OnInitializeComplete(){
    // Set Nodes to move around
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        // mean = 12, median = 8, min = 6, max = 80
        node->GetObject<RandomWalk2dMobilityModel>()->SetAttribute(
            "Speed",
            StringValue("ns3::ParetoRandomVariable[Bound=80.0|Scale=6.0|Shape=2.0]"));
    }
}

void
LraExample::CreateDevices()
{
    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    YansWifiPhyHelper wifiPhy;
    wifiPhy.Set("CcaEdThreshold", DoubleValue (0));
    wifiPhy.Set("CcaSensitivity", DoubleValue (0));
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    netDevices = wifi.Install(wifiPhy, wifiMac, nodes);

    if (pcap)
    {
        wifiPhy.EnablePcapAll(std::string("lra"));
    }
}

void
LraExample::InstallInternetStack()
{
    LraHelper lra;
    InternetStackHelper stack;

    stack.SetRoutingHelper(lra);
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.0.0.0");
    ipv4Interfaces = address.Assign(netDevices);
    m_sinkAddress = ipv4Interfaces.GetAddress(nodes.GetN() - 1);

    InitNodesRouting();

    if (printRoutes)
    {
        Ptr<OutputStreamWrapper> routingStream =
            Create<OutputStreamWrapper>("lra.routes", std::ios::out);
        LraHelper::PrintRoutingTableAllAt(Seconds(startDelay-1), routingStream);
    }
}

void
LraExample::InitNodesRouting()
{    
    // Initialize LRA routing protocol for each node
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        Ptr<LraRoutingProtocol> lraRouting = node->GetObject<LraRoutingProtocol>();
        lraRouting->InitializeNode(m_sinkAddress, i);
    }
}

void
LraExample::InstallApplications()
{
    // Set each node to be both echo client and server
    int port = 9;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer echoServerApps = echoServer.Install(nodes);
    echoServerApps.Start(Seconds(0));
    echoServerApps.Stop(Seconds(totalTime) - Seconds(0.001));

    UdpEchoClientHelper echoClient(Address(m_sinkAddress), port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(n_packets));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(32));

    Time jitter = Time(Seconds(startDelay));
    Simulator::Schedule(jitter, &PrintCheckpoint);
    for (uint32_t i = 0; i < nodes.GetN() - 1; ++i)
    {
        auto app = echoClient.Install(nodes.Get(i));
        int randa = rand() % 1000;
        app.Start(Seconds(startDelay + randa)); // needed to avoid collisions
        app.Stop(Seconds(totalTime) - Seconds(0.001));
    }

    // Trace package send and receiving to calculate loss
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoServer/RxWithAddresses",
                    MakeCallback(&LraExample::LogMessageResponse, this));
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/TxWithAddresses",
                    MakeCallback(&LraExample::LogMessageSend, this));
}

void
LraExample::LogMessageResponse(std::string ctx,
                               Ptr<const Packet> packet,
                               const Address& srcAddress,
                               const Address& destAddress)
{
    NS_LOG_INFO("Message received " << ctx << *packet
                                    << InetSocketAddress::ConvertFrom(srcAddress).GetIpv4()
                                    << InetSocketAddress::ConvertFrom(destAddress).GetIpv4());
    auto ipAddr = InetSocketAddress::ConvertFrom(srcAddress).GetIpv4();
    m_packetsSentByNodes[ipAddr]--;
}

void
LraExample::LogMessageSend(std::string ctx,
                           Ptr<const Packet> packet,
                           const Address& srcAddress,
                           const Address& destAddress)
{
    NS_LOG_INFO("Message Sent " << ctx << *packet
                                << InetSocketAddress::ConvertFrom(srcAddress).GetIpv4()
                                << InetSocketAddress::ConvertFrom(destAddress).GetIpv4());
    auto nodeId = LraExample::GetNodeIdFromContext(ctx);
    auto nodeAddress = LraExample::GetNodeAddressFromId(nodeId);
    m_packetsSentByNodes[nodeAddress]++;
    tot_acnt++;
}

uint32_t
LraExample::GetNodeIdFromContext(std::string ctx)
{
    const std::size_t startIndex = ctx.find_first_of('/', 1 /*skip first occurence*/);
    const std::size_t endIndex = ctx.find_first_of('/', startIndex + 1); // find second occurence
    return std::stoul(
        ctx.substr(startIndex + 1,
                   endIndex - startIndex - 1)); // take substring from first to second occurences
}

Ipv4Address
LraExample::GetNodeAddressFromId(uint32_t id)
{
    return ipv4Interfaces.GetAddress(id);
}

uint32_t
LraExample::GetApplicationIndexFromContext(std::string ctx)
{
    const std::size_t nodeIdStartIndex = ctx.find_first_of('/', 1 /*skip first occurence*/);
    const std::size_t nodeIdEndIndex =
        ctx.find_first_of('/', nodeIdStartIndex + 1); // find second occurence

    const std::size_t appIdStartIndex = ctx.find_first_of('/', nodeIdEndIndex + 1);
    const std::size_t appIdEndIndex = ctx.find_first_of('/', appIdStartIndex + 1);
    return std::stoul(ctx.substr(appIdStartIndex + 1, appIdEndIndex - appIdStartIndex - 1));
}