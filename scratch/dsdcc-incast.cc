#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Incast");

std::stringstream filePlotQueue;
std::stringstream filePlotThroughput;

static bool firstCwnd = true;
static bool firstSshThr = true;
static bool firstRtt = true;
static bool firstRto = true;
static Ptr<OutputStreamWrapper> cWndStream;
static Ptr<OutputStreamWrapper> ssThreshStream;
static Ptr<OutputStreamWrapper> rttStream;
static Ptr<OutputStreamWrapper> rtoStream;
static uint32_t cWndValue;
static uint32_t ssThreshValue;
static double interval = 0.001;
uint64_t flowRecvBytes = 0;

static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;

  if (!firstSshThr)
    {
      *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue << std::endl;
    }
}

static void
SsThreshTracer (uint32_t oldval, uint32_t newval)
{
  if (firstSshThr)
    {
      *ssThreshStream->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr = false;
    }
  *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue = newval;

  if (!firstCwnd)
    {
      *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue << std::endl;
    }
}

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
RtoTracer (Time oldval, Time newval)
{
  if (firstRto)
    {
      *rtoStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRto = false;
    }
  *rtoStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}


static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name)
{
  AsciiTraceHelper ascii;
  ssThreshStream = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&SsThreshTracer));
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceRto (std::string rto_tr_file_name)
{
  AsciiTraceHelper ascii;
  rtoStream = ascii.CreateFileStream (rto_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/RTO", MakeCallback (&RtoTracer));
}

void
CheckQueueSize (Ptr<QueueDisc> queue, std::string filePlotQueue)
{
  uint32_t qSize = StaticCast<RedQueueDisc> (queue)->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.0001), &CheckQueueSize, queue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

void
ThroughputPerSecond(Ptr<PacketSink> sink1Apps,std::string filePlotThrough)
{
	uint32_t totalRecvBytes = sink1Apps->GetTotalRx();
	uint32_t currentPeriodRecvBytes = totalRecvBytes - flowRecvBytes;

	flowRecvBytes = totalRecvBytes;

	Simulator::Schedule (Seconds(interval), &ThroughputPerSecond, sink1Apps, filePlotThrough);
	std::ofstream fPlotThroughput (filePlotThrough.c_str (), std::ios::out | std::ios::app);
	fPlotThroughput << Simulator::Now().GetSeconds() << " " << currentPeriodRecvBytes * 8 / (interval * 1000000) << std::endl;
}

int main (int argc, char *argv[])
{
  // Configure information
  std::string prefix_file_name = "Incast";
  uint32_t sendNum = 20;//(1~50)
  std::string transport_port = "TcpDsdcc";// TcpDctcp  TcpCubic TcpDsdcc TcpDcvegas
  std::string queue_disc_type = "RedQueueDisc";//only for dctcp

  std::string queue_limit = "250p";//94
  double K = 65;//18
  uint32_t DcvegasNqK = 60;

  std::string bandwidth = "10Gbps";
  std::string delay = "0.01ms";
  std::string bottleneck_bw = "10Gbps";
  std::string bottleneck_delay = "0.01ms";

  uint32_t IP_PACKET_SIZE = 1500;
  uint32_t TCP_SEGMENT = IP_PACKET_SIZE-40;
  double data_mbytes = 2*1024*1024;

  double minRto = 25;
  uint32_t initialCwnd = 2;

  double start_time = 0;
  double stop_time = 1;

  bool tracing = true;

  // Create directory information
 time_t rawtime;
 struct tm * timeinfo;
 char buffer[80];
 time (&rawtime);
 timeinfo = localtime(&rawtime);
//
 strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
 std::string currentTime (buffer);

  //random variable generation
//  srand(time(NULL));
//  SeedManager::SetSeed(rand());
//  Ptr<UniformRandomVariable> random = CreateObject<UniformRandomVariable> ();

  CommandLine cmd;
  cmd.AddValue ("DcvegasNqK", "dcvegas nq k", DcvegasNqK);
  cmd.AddValue ("sendNum","Number of left and right side leaf nodes", sendNum);
  cmd.AddValue ("queuedisc","type of queuedisc", queue_disc_type);
  cmd.AddValue ("bandwidth", "Access bandwidth", bandwidth);
  cmd.AddValue ("delay", "Access delay", delay);
  cmd.AddValue ("bottleneck_bw", "Bottleneck bandwidth", bottleneck_bw);
  cmd.AddValue ("bottleneck_delay", "Bottleneck delay", bottleneck_delay);
  cmd.AddValue ("TCP_SEGMENT", "Packet size", TCP_SEGMENT);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit, 0 means infinite", data_mbytes);
  cmd.AddValue ("start_time", "Start Time", start_time);
  cmd.AddValue ("stop_time", "Stop Time", stop_time);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpDctcp, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_port);
  cmd.Parse (argc,argv);

  // To enable logging
  LogComponentEnable("Incast", LOG_LEVEL_INFO);
//  LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
//  LogComponentEnable("RedQueueMMDisc", LOG_LEVEL_DEBUG);
//  LogComponentEnable("FifoQueueDisc", LOG_LEVEL_LOGIC);
//  LogComponentEnable("Ipv4QueueDiscItem", LOG_LEVEL_DEBUG);
//  LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
 LogComponentEnable("TcpDsdcc", LOG_LEVEL_DEBUG);

  NS_LOG_INFO ("Configure TcpSocket");
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + transport_port));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (minRto)));
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (true));
  // Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initialCwnd));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (100)));

  // for dsdcc
  Config::SetDefault ("ns3::TcpDsdcc::DsdccNqK", UintegerValue (60));
  // for dcvegas
  Config::SetDefault ("ns3::TcpDcvegas::DcvegasNqK", UintegerValue (60));
  if(transport_port.compare("TcpDctcp") == 0 || transport_port.compare("TcpDsdcc") == 0)
  {
	  NS_LOG_INFO ("Configure ECN and RED");
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseEcn", BooleanValue (true));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MeanPktSize", UintegerValue (IP_PACKET_SIZE));
	  Config::SetDefault("ns3::"+queue_disc_type+"::QW", DoubleValue (1));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MinTh", DoubleValue (K));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MaxTh", DoubleValue (K));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::Gentle", BooleanValue (false));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseHardDrop", BooleanValue (false));
  }
  else
  {
	  NS_LOG_INFO ("Configure Fifo");
	  queue_disc_type = "FifoQueueDisc";
	  Config::SetDefault("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
  }

  // Create sendNum, receiver, and switches
  NodeContainer switches;
  switches.Create(1);
  NodeContainer senders;
  senders.Create(sendNum);
  NodeContainer receiver;
  receiver.Create(1);
  NS_LOG_INFO (senders.Get(0)->GetId());
  NS_LOG_INFO (senders.Get(1)->GetId());
  NS_LOG_INFO (senders.Get(2)->GetId());

  // Configure channel attributes
  PointToPointHelper ptpLink;
  ptpLink.SetDeviceAttribute("DataRate", StringValue(bandwidth));
  ptpLink.SetChannelAttribute("Delay", StringValue(delay));

  PointToPointHelper neckLink;
  neckLink.SetDeviceAttribute("DataRate", StringValue(bottleneck_bw));
  neckLink.SetChannelAttribute("Delay", StringValue(bottleneck_delay));

  // Install InternetStack
  InternetStackHelper stack;
  stack.InstallAll();

  // Configure TrafficControlHelper
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::"+queue_disc_type);
  NS_LOG_INFO ("Install " << queue_disc_type);

  // Configure Ipv4AddressHelper
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");

  // Configure net devices in nodes and connect sendNum with switches
  for(uint32_t i = 0; i<sendNum; i++)
  {
	  NetDeviceContainer devices;
	  devices = ptpLink.Install(senders.Get(i), switches.Get(0));
	  tchRed.Install(devices);
	  address.NewNetwork();
	  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  }

  // Connect switches with receiver
  NetDeviceContainer devices;
  devices = neckLink.Install(switches.Get(0), receiver.Get(0));

  // Install queueDiscs in switch
  QueueDiscContainer queueDiscs;
  queueDiscs = tchRed.Install(devices);

  address.NewNetwork();
  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  Ipv4InterfaceContainer sink_interfaces;
  sink_interfaces.Add(interfaces.Get(1));

  // Configure routing
  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Configure port/address and install application, establish connection
  NS_LOG_INFO ("Build connections");
  uint16_t port = 50000;

  // long flow
  for(uint16_t i=0; i<senders.GetN(); i++)
  {
	  // BulkSend
	  if(i != 0 && i != 1 && i != 2)
	  {
		  // BulkSendHelper ftp("ns3::TcpSocketFactory", InetSocketAddress(sink_interfaces.GetAddress(0,0),port));
		  // ftp.SetAttribute("SendSize", UintegerValue(TCP_SEGMENT));
		  // ftp.SetAttribute("MaxBytes", UintegerValue(data_mbytes/sendNum));
		  // ApplicationContainer sourceApp = ftp.Install(senders.Get(i));
		  // sourceApp.Start(Seconds(start_time+0.03));
	//	  sourceApp.Stop(Seconds(stop_time));
	  }
	  else
	  {
		  BulkSendHelper ftp("ns3::TcpSocketFactory", InetSocketAddress(sink_interfaces.GetAddress(0,0),port));
		  ftp.SetAttribute("SendSize", UintegerValue(TCP_SEGMENT));
		  ftp.SetAttribute("MaxBytes", UintegerValue(0));
		  ApplicationContainer sourceApp = ftp.Install(senders.Get(i));
		  sourceApp.Start(Seconds(start_time));
	//	  sourceApp.Stop(Seconds(stop_time));
	  }

  }

  PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sinkHelper.Install(receiver.Get(0));
  sinkApp.Start(Seconds(start_time));
  sinkApp.Stop(Seconds(stop_time));

  // Collect data
  std::string dir = "incast/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
  std::cout << "Data directory:" << dir << std::endl;

  if (tracing)
  {
      std::string dirToSave = "mkdir -p " + dir;
	  system (dirToSave.c_str ());

	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceCwnd, dir+"/cwnd.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceSsThresh, dir+"/ssth.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRtt, dir+"/rtt.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRto, dir+"/rto.data");

	  // Get queue size
	  filePlotQueue <<  dir << "/" << "queue-size.plotme";
	//   remove (filePlotQueue.str ().c_str());
	  Ptr<QueueDisc> queue = queueDiscs.Get(0);
	  Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());

	  // Get delay
	  Simulator::Schedule (Seconds (start_time + 0.030001), &TraceRtt, "rtt.data");

	  // Get throughput
	  filePlotThroughput << dir << "/" << "throughput.plotme";
	  //remove (filePlotThroughput.str ().c_str());
	  Simulator::ScheduleNow (&ThroughputPerSecond, sinkApp.Get(0)->GetObject<PacketSink>(), filePlotThroughput.str ());
  }

  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  //AnimationInterface anim("dctcpVScubic.xml");
  Simulator::Stop (Seconds(stop_time));
  Simulator::Run ();

  // Get information from FlowMonitor
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  double max_fct=0;
  uint32_t count=0;

 for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
   {
     Ipv4FlowClassifier::FiveTuple FiveTuple = classifier->FindFlow (i->first);
     std::cout << "Flow " << i->first << " (" << FiveTuple.sourceAddress << " -> " << FiveTuple.destinationAddress << ")\n";
     std::cout << "  Statr time: " << i->second.timeFirstTxPacket << "\n";
     std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
     std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
     std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 1000000 / (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
     std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
     std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
     std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 1000000/ (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
     std::cout << "  FCT:  " << (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " s\n";
     if(((i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds()>max_fct) && (count<sendNum))
     {
   	  max_fct = (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds();
     }
     count++;
   }
 double goodput = data_mbytes * 8.0 / 1000000 / max_fct;
 std::cout << "goodput: " << goodput << " Mbps" << std::endl;
 std::cout << "query FCT: " << max_fct << " s" << std::endl;

  // Collect data
  NS_LOG_INFO ("Collect data.");

  std::ofstream myfile;
  // remove ((transport_port+"-incast-goodput.dat").c_str());
  myfile.open (transport_port+"-incast-goodput.dat", std::fstream::in | std::fstream::out | std::fstream::app);
  myfile << sendNum << " " << goodput << "\n";
  myfile.close();

  Simulator::Destroy ();
  return 0;
}