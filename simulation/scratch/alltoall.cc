/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
*/

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"
#define HEADER_SIZE 48

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <time.h> 
#include "ns3/core-module.h"
#include "ns3/qbb-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/error-model.h"
#include <ns3/rdma.h>
#include <ns3/rdma-client.h>
#include <ns3/rdma-client-helper.h>
#include <ns3/rdma-driver.h>
#include <ns3/switch-node.h>
#include <ns3/sim-setting.h>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");

bool response_HT = false;
uint32_t route_mode = 0;
uint32_t rps_seed = 42;
uint32_t cc_mode = 1;
bool enable_pfc = true;
bool enable_qcn = true, use_dynamic_pfc_threshold = true;
uint32_t packet_payload_size = 1000, l2_chunk_size = 0, l2_ack_interval = 0;
double pause_time = 5, simulator_stop_time = 3.01;
std::string data_rate, link_delay, topology_file, flow_file, trace_file, trace_output_file;
uint32_t group_size, group_num, intra_group_stride, inter_group_stride;
uint32_t alltoall_message_size;  // Bytes

std::string fct_output_file = "fct.txt";
std::string pfc_output_file = "pfc.txt";
std::string alltoall_output_file = "alltoall.txt";

double alpha_resume_interval = 55, rp_timer, ewma_gain = 1 / 16;
double rate_decrease_interval = 4;
uint32_t fast_recovery_times = 5;
std::string rate_ai, rate_hai, min_rate = "100Mb/s";
std::string dctcp_rate_ai = "1000Mb/s";

bool clamp_target_rate = false, l2_back_to_zero = false;
double error_rate_per_link = 0.0;
uint32_t has_win = 1;
uint32_t global_t = 1;
uint32_t mi_thresh = 5;
bool var_win = false, fast_react = true;
bool multi_rate = true;
bool sample_feedback = false;
double pint_log_base = 1.05;
double pint_prob = 1.0;
double u_target = 0.95;
uint32_t int_multi = 1;
bool rate_bound = true;

uint32_t ack_high_prio = 0;
uint64_t link_down_time = 0;
uint32_t link_down_A = 0, link_down_B = 0;

uint32_t enable_trace = 1;

double retran_timeout = 0.008; // s 

uint32_t buffer_size = 16;

unordered_map<uint64_t, uint32_t> rate2kmax, rate2kmin;
unordered_map<uint64_t, double> rate2pmax;

/************************************************
 * Runtime varibles
 ***********************************************/
std::ifstream topof, tracef;

uint32_t flow_num;

NodeContainer n;

uint64_t nic_rate;

uint64_t maxRtt, maxBdp;

struct Interface{
	uint32_t idx;
	bool up;
	uint64_t delay;
	uint64_t bw;

	Interface() : idx(0), up(false){}
};
map<Ptr<Node>, map<Ptr<Node>, Interface> > nbr2if;
// Mapping destination to next hop for each node: <node, <dest, <nexthop0, ...> > >
map<Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node> > > > nextHop;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairDelay;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairTxDelay;
map<uint32_t, map<uint32_t, uint64_t> > pairBw;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairBdp;
map<uint32_t, map<uint32_t, uint64_t> > pairRtt;

std::vector<Ipv4Address> serverAddress;

// maintain port number for each host pair
std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t> > portNumder;

Ipv4Address node_id_to_ip(uint32_t id){
	return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

uint32_t ip_to_node_id(Ipv4Address ip){
	return (ip.Get() >> 8) & 0xffff;
}

/************************
 * Alltoall
 ************************/

class AlltoallJob {
public:
    uint32_t job_id;
    std::vector<uint32_t> node_ids;
    uint32_t tensor_size;  // Bytes
    Time job_start_time;
    // runtime
    uint32_t finished_transfer_num;  // there are total len(node_ids) * (len(node_ids) - 1) transfers
	// statistics
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint64_t> > transfer_time;  // transfer_time[src][dst] is the time of the transfer from src to dst
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint64_t> > transfer_start_time;  // transfer_start_time[src][dst] is the start time of the transfer from src to dst
	Time job_end_time;

	AlltoallJob(uint32_t id, const std::vector<uint32_t>& nodes, uint32_t size, double start_time);
	void ScheduleStartEvent();
	void write_statics(FILE* fout);
};

class AlltoallSubflow {
public:
    uint32_t message_size;
    uint32_t src_node, dst_node, dst_port;
    AlltoallJob* alltoall_job; // Pointer to AlltoallJob

    AlltoallSubflow(uint32_t size, uint32_t src, uint32_t dst, uint32_t port, AlltoallJob* job);
    void ScheduleStartEvent();
};

uint32_t qp_idx = 0;
// qp_id -> AlltoallSubflow*
std::unordered_map<uint32_t, AlltoallSubflow*> qpId2subflow;


AlltoallJob::AlltoallJob(uint32_t id, const std::vector<uint32_t>& nodes, uint32_t size, double start_time)
	: job_id(id), node_ids(nodes), tensor_size(size), finished_transfer_num(0) {
	job_start_time = Seconds(start_time);
}

void AlltoallJob::ScheduleStartEvent() {
	// each node start a AlltoallSubflow
	for (uint32_t i = 0; i < node_ids.size(); i++){
		for (uint32_t j = 0; j < node_ids.size(); j++){
			if (i == j) continue;
			uint32_t size = tensor_size / node_ids.size();
			AlltoallSubflow* subflow = new AlltoallSubflow(size, node_ids[i], node_ids[j], 100, this);
			Simulator::Schedule(job_start_time, &AlltoallSubflow::ScheduleStartEvent, subflow);
		}
	}
}

void AlltoallJob::write_statics(FILE* fout) {
	// check if fout is valid
	if (fout == NULL) {
		std::cerr << "Error: Invalid file pointer for alltoall statics" << std::endl;
		return;
	}
	// write statics to fout
	uint64_t total_time = (job_end_time - job_start_time).GetTimeStep();
	uint64_t standalone_jct = (tensor_size / node_ids.size() * 8000000000lu / pairBw[node_ids[0]][node_ids[1]]) * (node_ids.size() - 1)  + pairRtt[node_ids[0]][node_ids[1]];
	fprintf(fout, "%u %lu %lu %lu\n", job_id, job_start_time.GetTimeStep(), total_time, standalone_jct);
	fflush(fout);
}

AlltoallSubflow::AlltoallSubflow(uint32_t size, uint32_t src, uint32_t dst, uint32_t port, AlltoallJob* job)
	: message_size(size), src_node(src), dst_node(dst), dst_port(port), alltoall_job(job) {
}

void AlltoallSubflow::ScheduleStartEvent() {
	uint32_t port = portNumder[src_node][dst_node]++; // Use current subflow's src and dst
	RdmaClientHelper clientHelper(0, serverAddress[src_node], serverAddress[dst_node], port, dst_port, 
		message_size,
		has_win ? (global_t == 1 ? maxBdp : pairBdp[n.Get(src_node)][n.Get(dst_node)]) : 0,
		global_t == 1 ? maxRtt : pairRtt[src_node][dst_node],
		qp_idx, retran_timeout);
	ApplicationContainer appCon = clientHelper.Install(n.Get(src_node));
	qpId2subflow[qp_idx] = this;
	alltoall_job->transfer_start_time[src_node][dst_node] = Simulator::Now().GetTimeStep();
	qp_idx++;
	appCon.Start(Time(0));
}


uint32_t finish_cnt = 0;
void qp_finish(FILE* fout, Ptr<RdmaQueuePair> q){

    uint32_t sid = ip_to_node_id(q->sip), did = ip_to_node_id(q->dip);
    
    // Original statistics recording
    uint64_t base_rtt = pairRtt[sid][did], b = pairBw[sid][did];
    uint32_t total_bytes = q->m_size + ((q->m_size-1) / packet_payload_size + 1) * (CustomHeader::GetStaticWholeHeaderSize() - IntHeader::GetStaticSize());
    uint64_t standalone_fct = base_rtt + total_bytes * 8000000000lu / b;
    fprintf(fout, "%u %u %u %u %lu %lu %lu %lu %lu %u %u\n",
	 	sid, did, q->sport, q->dport, q->m_size, q->startTime.GetTimeStep(), 
		(Simulator::Now() - q->startTime).GetTimeStep(), 
		standalone_fct, q->m_retx_num * packet_payload_size, 
		q->m_timeout_count,
		q->m_qpId);
    fflush(fout);
    // Find corresponding subflow
    auto it = qpId2subflow.find(q->m_qpId);
    if (it == qpId2subflow.end()) {
        NS_ASSERT(false);  // If subflow not found, assert error
    }
    AlltoallSubflow* subflow = it->second;
    NS_ASSERT(sid == subflow->src_node && did == subflow->dst_node);
    AlltoallJob* alltoall_job = subflow->alltoall_job;

    // Record statistics
    uint64_t finish_time = Simulator::Now().GetTimeStep();
    alltoall_job->transfer_time[sid][did] = finish_time - alltoall_job->transfer_start_time[sid][did];
    alltoall_job->finished_transfer_num++;

    if (alltoall_job->finished_transfer_num == alltoall_job->node_ids.size() * (alltoall_job->node_ids.size() - 1)) {
		alltoall_job->job_end_time = Simulator::Now();
	}

    finish_cnt++;
    // Remove rxQp from the receiver
    Ptr<Node> dstNode = n.Get(did);
    Ptr<RdmaDriver> rdma = dstNode->GetObject<RdmaDriver> ();
    rdma->m_rdma->DeleteRxQp(q->sip.Get(), q->m_pg, q->sport);
    
    delete subflow;
}

void get_pfc(FILE* fout, Ptr<QbbNetDevice> dev, uint32_t type){
	fprintf(fout, "%lu %u %u %u %u\n", Simulator::Now().GetTimeStep(), dev->GetNode()->GetId(), dev->GetNode()->GetNodeType(), dev->GetIfIndex(), type);
}

void CalculateRoute(Ptr<Node> host){
	// queue for the BFS.
	vector<Ptr<Node> > q;
	// Distance from the host to each node.
	map<Ptr<Node>, int> dis;
	map<Ptr<Node>, uint64_t> delay;
	map<Ptr<Node>, uint64_t> txDelay;
	map<Ptr<Node>, uint64_t> bw;
	// init BFS.
	q.push_back(host);
	dis[host] = 0;
	delay[host] = 0;
	txDelay[host] = 0;
	bw[host] = 0xfffffffffffffffflu;
	// BFS.
	for (int i = 0; i < (int)q.size(); i++){
		Ptr<Node> now = q[i];
		int d = dis[now];
		for (auto it = nbr2if[now].begin(); it != nbr2if[now].end(); it++){
			// skip down link
			if (!it->second.up)
				continue;
			Ptr<Node> next = it->first;
			// If 'next' have not been visited.
			if (dis.find(next) == dis.end()){
				dis[next] = d + 1;
				delay[next] = delay[now] + it->second.delay;
				txDelay[next] = txDelay[now] + packet_payload_size * 1000000000lu * 8 / it->second.bw;
				bw[next] = std::min(bw[now], it->second.bw);
				// we only enqueue switch, because we do not want packets to go through host as middle point
				if (next->GetNodeType() == 1)
					q.push_back(next);
			}
			// if 'now' is on the shortest path from 'next' to 'host'.
			if (d + 1 == dis[next]){
				nextHop[next][host].push_back(now);
			}
		}
	}
	for (auto it : delay)
		pairDelay[it.first][host] = it.second;
	for (auto it : txDelay)
		pairTxDelay[it.first][host] = it.second;
	for (auto it : bw)
		pairBw[it.first->GetId()][host->GetId()] = it.second;
}

void CalculateRoutes(NodeContainer &n){
	for (int i = 0; i < (int)n.GetN(); i++){
		Ptr<Node> node = n.Get(i);
		if (node->GetNodeType() == 0)
			CalculateRoute(node);
	}
}

void SetRoutingEntries(){
	// For each node.
	for (auto i = nextHop.begin(); i != nextHop.end(); i++){
		Ptr<Node> node = i->first;
		auto &table = i->second;
		for (auto j = table.begin(); j != table.end(); j++){
			// The destination node.
			Ptr<Node> dst = j->first;
			// The IP address of the dst.
			Ipv4Address dstAddr = dst->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
			// The next hops towards the dst.
			vector<Ptr<Node> > nexts = j->second;
			for (int k = 0; k < (int)nexts.size(); k++){
				Ptr<Node> next = nexts[k];
				uint32_t interface = nbr2if[node][next].idx;
				if (node->GetNodeType() == 1)
					DynamicCast<SwitchNode>(node)->AddTableEntry(dstAddr, interface);
				else{
					node->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(dstAddr, interface);
				}
			}
		}
	}
}

// take down the link between a and b, and redo the routing
void TakeDownLink(NodeContainer n, Ptr<Node> a, Ptr<Node> b){
	if (!nbr2if[a][b].up)
		return;
	// take down link between a and b
	nbr2if[a][b].up = nbr2if[b][a].up = false;
	nextHop.clear();
	CalculateRoutes(n);
	// clear routing tables
	for (uint32_t i = 0; i < n.GetN(); i++){
		if (n.Get(i)->GetNodeType() == 1)
			DynamicCast<SwitchNode>(n.Get(i))->ClearTable();
		else
			n.Get(i)->GetObject<RdmaDriver>()->m_rdma->ClearTable();
	}
	DynamicCast<QbbNetDevice>(a->GetDevice(nbr2if[a][b].idx))->TakeDown();
	DynamicCast<QbbNetDevice>(b->GetDevice(nbr2if[b][a].idx))->TakeDown();
	// reset routing table
	SetRoutingEntries();

	// redistribute qp on each host
	for (uint32_t i = 0; i < n.GetN(); i++){
		if (n.Get(i)->GetNodeType() == 0)
			n.Get(i)->GetObject<RdmaDriver>()->m_rdma->RedistributeQp();
	}
}

uint64_t get_nic_rate(NodeContainer &n){
	for (uint32_t i = 0; i < n.GetN(); i++)
		if (n.Get(i)->GetNodeType() == 0)
			return DynamicCast<QbbNetDevice>(n.Get(i)->GetDevice(1))->GetDataRate().GetBitRate();
}

int main(int argc, char *argv[])
{
	clock_t begint, endt;
	begint = clock();
#ifndef PGO_TRAINING
	if (argc > 1)
#else
	if (true)
#endif
	{
		//Read the configuration file
		std::ifstream conf;
#ifndef PGO_TRAINING
		conf.open(argv[1]);
		if (!conf.is_open()) {
			std::cerr << "Error: Cannot open config file: " << argv[1] << std::endl;
			std::cerr << "Current working directory: " << std::system("pwd") << std::endl;
			fflush(stderr);
			return 1;
		}
		std::cout << "Successfully opened config file: " << argv[1] << std::endl;
#else
		conf.open(PATH_TO_PGO_CONFIG);
		if (!conf.is_open()) {
			std::cerr << "Error: Cannot open config file: " << PATH_TO_PGO_CONFIG << std::endl;
			fflush(stderr);
			return 1;
		}
#endif
		while (!conf.eof())
		{
			std::string key;
			conf >> key;

			//std::cout << conf.cur << "\n";

			if (key.compare("ENABLE_QCN") == 0)
			{
				uint32_t v;
				conf >> v;
				enable_qcn = v;
				if (enable_qcn)
					std::cout << "ENABLE_QCN\t\t\t" << "Yes" << "\n";
				else
					std::cout << "ENABLE_QCN\t\t\t" << "No" << "\n";
			} else if (key.compare("RESPONSE_HT") == 0) {
				uint32_t v;
				conf >> v;
				response_HT = v;
				if (response_HT)
					std::cout << "RESPONSE_HT\t\t\t" << "Yes" << "\n";
				else
					std::cout << "RESPONSE_HT\t\t\t" << "No" << "\n";
			} else if (key.compare("TIMEOUT") == 0) {
				double v;
				conf >> v;
				retran_timeout = v;
				std::cout << "TIMEOUT\t\t\t" << retran_timeout << "\n";
			} else if (key.compare("ROUTE_MODE") == 0) {
				uint32_t v;
				conf >> v;
				route_mode = v;
				std::cout << "ROUTE_MODE\t\t\t";
				if (route_mode == 0)
					std::cout << "ECMP\n";
				else if (route_mode == 1)
					std::cout << "Adaptive Routing\n";
				else if (route_mode == 2)
					std::cout << "Random Packet Spray\n";
				else
					std::cout << "Unknown\n";
			} else if (key.compare("RPS_SEED") == 0) {
				uint32_t v;
				conf >> v;
				rps_seed = v;
				std::cout << "RPS_SEED\t\t\t" << rps_seed << "\n";
			}
			else if (key.compare("USE_DYNAMIC_PFC_THRESHOLD") == 0)
			{
				uint32_t v;
				conf >> v;
				use_dynamic_pfc_threshold = v;
				if (use_dynamic_pfc_threshold)
					std::cout << "USE_DYNAMIC_PFC_THRESHOLD\t" << "Yes" << "\n";
				else
					std::cout << "USE_DYNAMIC_PFC_THRESHOLD\t" << "No" << "\n";
			}
			else if (key.compare("CLAMP_TARGET_RATE") == 0)
			{
				uint32_t v;
				conf >> v;
				clamp_target_rate = v;
				if (clamp_target_rate)
					std::cout << "CLAMP_TARGET_RATE\t\t" << "Yes" << "\n";
				else
					std::cout << "CLAMP_TARGET_RATE\t\t" << "No" << "\n";
			}
			else if (key.compare("PAUSE_TIME") == 0)
			{
				double v;
				conf >> v;
				pause_time = v;
				std::cout << "PAUSE_TIME\t\t\t" << pause_time << "\n";
			}
			else if (key.compare("DATA_RATE") == 0)
			{
				std::string v;
				conf >> v;
				data_rate = v;
				std::cout << "DATA_RATE\t\t\t" << data_rate << "\n";
			}
			else if (key.compare("LINK_DELAY") == 0)
			{
				std::string v;
				conf >> v;
				link_delay = v;
				std::cout << "LINK_DELAY\t\t\t" << link_delay << "\n";
			}
			else if (key.compare("PACKET_PAYLOAD_SIZE") == 0)
			{
				uint32_t v;
				conf >> v;
				packet_payload_size = v;
				std::cout << "PACKET_PAYLOAD_SIZE\t\t" << packet_payload_size << "\n";
			}
			else if (key.compare("L2_CHUNK_SIZE") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_chunk_size = v;
				std::cout << "L2_CHUNK_SIZE\t\t\t" << l2_chunk_size << "\n";
			}
			else if (key.compare("L2_ACK_INTERVAL") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_ack_interval = v;
				std::cout << "L2_ACK_INTERVAL\t\t\t" << l2_ack_interval << "\n";
			}
			else if (key.compare("L2_BACK_TO_ZERO") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_back_to_zero = v;
				if (l2_back_to_zero)
					std::cout << "L2_BACK_TO_ZERO\t\t\t" << "Yes" << "\n";
				else
					std::cout << "L2_BACK_TO_ZERO\t\t\t" << "No" << "\n";
			}
			else if (key.compare("TOPOLOGY_FILE") == 0)
			{
				std::string v;
				conf >> v;
				topology_file = v;
				std::cout << "TOPOLOGY_FILE\t\t\t" << topology_file << "\n";
			}
			else if (key.compare("GROUP_SIZE") == 0) {
				uint32_t v;
				conf >> v;
				group_size = v;
				std::cout << "GROUP_SIZE\t\t\t" << group_size << "\n";
			}
			else if (key.compare("GROUP_NUM") == 0) {
				uint32_t v;
				conf >> v;
				group_num = v;
				std::cout << "GROUP_NUM\t\t\t" << group_num << "\n";
			}
			else if (key.compare("INTRA_GROUP_STRIDE") == 0) {
				uint32_t v;
				conf >> v;
				intra_group_stride = v;
				std::cout << "INTRA_GROUP_STRIDE\t\t" << intra_group_stride << "\n";
			}
			else if (key.compare("INTER_GROUP_STRIDE") == 0) {
				uint32_t v;
				conf >> v;
				inter_group_stride = v;
				std::cout << "INTER_GROUP_STRIDE\t\t" << inter_group_stride << "\n";
			}
			else if (key.compare("ALLTOALL_MESSAGE_SIZE") == 0) {
				uint32_t v;
				conf >> v;
				alltoall_message_size = v;
				std::cout << "ALLTOALL_MESSAGE_SIZE\t\t" << alltoall_message_size << "\n";
			}
			else if (key.compare("ALLTOALL_OUTPUT_FILE") == 0) {
				std::string v;
				conf >> v;
				alltoall_output_file = v;
				std::cout << "ALLTOALL_OUTPUT_FILE\t\t" << alltoall_output_file << "\n";
			}
			else if (key.compare("TRACE_FILE") == 0)
			{
				std::string v;
				conf >> v;
				trace_file = v;
				std::cout << "TRACE_FILE\t\t\t" << trace_file << "\n";
			}
			else if (key.compare("TRACE_OUTPUT_FILE") == 0)
			{
				std::string v;
				conf >> v;
				trace_output_file = v;
				if (argc > 2)
				{
					trace_output_file = trace_output_file + std::string(argv[2]);
				}
				std::cout << "TRACE_OUTPUT_FILE\t\t" << trace_output_file << "\n";
			}
			else if (key.compare("SIMULATOR_STOP_TIME") == 0)
			{
				double v;
				conf >> v;
				simulator_stop_time = v;
				std::cout << "SIMULATOR_STOP_TIME\t\t" << simulator_stop_time << "\n";
			}
			else if (key.compare("ALPHA_RESUME_INTERVAL") == 0)
			{
				double v;
				conf >> v;
				alpha_resume_interval = v;
				std::cout << "ALPHA_RESUME_INTERVAL\t\t" << alpha_resume_interval << "\n";
			}
			else if (key.compare("RP_TIMER") == 0)
			{
				double v;
				conf >> v;
				rp_timer = v;
				std::cout << "RP_TIMER\t\t\t" << rp_timer << "\n";
			}
			else if (key.compare("EWMA_GAIN") == 0)
			{
				double v;
				conf >> v;
				ewma_gain = v;
				std::cout << "EWMA_GAIN\t\t\t" << ewma_gain << "\n";
			}
			else if (key.compare("FAST_RECOVERY_TIMES") == 0)
			{
				uint32_t v;
				conf >> v;
				fast_recovery_times = v;
				std::cout << "FAST_RECOVERY_TIMES\t\t" << fast_recovery_times << "\n";
			}
			else if (key.compare("RATE_AI") == 0)
			{
				std::string v;
				conf >> v;
				rate_ai = v;
				std::cout << "RATE_AI\t\t\t\t" << rate_ai << "\n";
			}
			else if (key.compare("RATE_HAI") == 0)
			{
				std::string v;
				conf >> v;
				rate_hai = v;
				std::cout << "RATE_HAI\t\t\t" << rate_hai << "\n";
			}
			else if (key.compare("ERROR_RATE_PER_LINK") == 0)
			{
				double v;
				conf >> v;
				error_rate_per_link = v;
				std::cout << "ERROR_RATE_PER_LINK\t\t" << error_rate_per_link << "\n";
			}
			else if (key.compare("CC_MODE") == 0){
				conf >> cc_mode;
				std::cout << "CC_MODE\t\t" << cc_mode << '\n';
			} else if (key.compare("PFC_ENABLE") == 0){
				uint32_t v;
				conf >> v;
				enable_pfc = v;
				std::cout << "PFC_ENABLE\t\t" << v << '\n';
			} else if (key.compare("RATE_DECREASE_INTERVAL") == 0){
				double v;
				conf >> v;
				rate_decrease_interval = v;
				std::cout << "RATE_DECREASE_INTERVAL\t\t" << rate_decrease_interval << "\n";
			}else if (key.compare("MIN_RATE") == 0){
				conf >> min_rate;
				std::cout << "MIN_RATE\t\t" << min_rate << "\n";
			}else if (key.compare("FCT_OUTPUT_FILE") == 0){
				conf >> fct_output_file;
				std::cout << "FCT_OUTPUT_FILE\t\t" << fct_output_file << '\n';
			}else if (key.compare("HAS_WIN") == 0){
				conf >> has_win;
				std::cout << "HAS_WIN\t\t" << has_win << "\n";
			}else if (key.compare("GLOBAL_T") == 0){
				conf >> global_t;
				std::cout << "GLOBAL_T\t\t" << global_t << '\n';
			}else if (key.compare("MI_THRESH") == 0){
				conf >> mi_thresh;
				std::cout << "MI_THRESH\t\t" << mi_thresh << '\n';
			}else if (key.compare("VAR_WIN") == 0){
				uint32_t v;
				conf >> v;
				var_win = v;
				std::cout << "VAR_WIN\t\t" << v << '\n';
			}else if (key.compare("FAST_REACT") == 0){
				uint32_t v;
				conf >> v;
				fast_react = v;
				std::cout << "FAST_REACT\t\t" << v << '\n';
			}else if (key.compare("U_TARGET") == 0){
				conf >> u_target;
				std::cout << "U_TARGET\t\t" << u_target << '\n';
			}else if (key.compare("INT_MULTI") == 0){
				conf >> int_multi;
				std::cout << "INT_MULTI\t\t\t\t" << int_multi << '\n';
			}else if (key.compare("RATE_BOUND") == 0){
				uint32_t v;
				conf >> v;
				rate_bound = v;
				std::cout << "RATE_BOUND\t\t" << rate_bound << '\n';
			}else if (key.compare("ACK_HIGH_PRIO") == 0){
				conf >> ack_high_prio;
				std::cout << "ACK_HIGH_PRIO\t\t" << ack_high_prio << '\n';
			}else if (key.compare("DCTCP_RATE_AI") == 0){
				conf >> dctcp_rate_ai;
				std::cout << "DCTCP_RATE_AI\t\t\t\t" << dctcp_rate_ai << "\n";
			}else if (key.compare("PFC_OUTPUT_FILE") == 0){
				conf >> pfc_output_file;
				std::cout << "PFC_OUTPUT_FILE\t\t\t\t" << pfc_output_file << '\n';
			}else if (key.compare("LINK_DOWN") == 0){
				conf >> link_down_time >> link_down_A >> link_down_B;
				std::cout << "LINK_DOWN\t\t\t\t" << link_down_time << ' '<< link_down_A << ' ' << link_down_B << '\n';
			}else if (key.compare("ENABLE_TRACE") == 0){
				conf >> enable_trace;
				std::cout << "ENABLE_TRACE\t\t\t\t" << enable_trace << '\n';
			}else if (key.compare("KMAX_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "KMAX_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					uint32_t k;
					conf >> rate >> k;
					rate2kmax[rate] = k;
					std::cout << ' ' << rate << ' ' << k;
				}
				std::cout<<'\n';
			}else if (key.compare("KMIN_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "KMIN_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					uint32_t k;
					conf >> rate >> k;
					rate2kmin[rate] = k;
					std::cout << ' ' << rate << ' ' << k;
				}
				std::cout<<'\n';
			}else if (key.compare("PMAX_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "PMAX_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					double p;
					conf >> rate >> p;
					rate2pmax[rate] = p;
					std::cout << ' ' << rate << ' ' << p;
				}
				std::cout<<'\n';
			}else if (key.compare("BUFFER_SIZE") == 0){
				conf >> buffer_size;
				std::cout << "BUFFER_SIZE\t\t\t\t" << buffer_size << '\n';
			}else if (key.compare("MULTI_RATE") == 0){
				int v;
				conf >> v;
				multi_rate = v;
				std::cout << "MULTI_RATE\t\t\t\t" << multi_rate << '\n';
			}else if (key.compare("SAMPLE_FEEDBACK") == 0){
				int v;
				conf >> v;
				sample_feedback = v;
				std::cout << "SAMPLE_FEEDBACK\t\t\t\t" << sample_feedback << '\n';
			}else if(key.compare("PINT_LOG_BASE") == 0){
				conf >> pint_log_base;
				std::cout << "PINT_LOG_BASE\t\t\t\t" << pint_log_base << '\n';
			}else if (key.compare("PINT_PROB") == 0){
				conf >> pint_prob;
				std::cout << "PINT_PROB\t\t\t\t" << pint_prob << '\n';
			}
			fflush(stdout);
		}
		conf.close();
	}
	else
	{
		std::cout << "Error: require a config file\n";
		fflush(stdout);
		return 1;
	}


	bool dynamicth = use_dynamic_pfc_threshold;

	Config::SetDefault("ns3::QbbNetDevice::PauseTime", UintegerValue(pause_time));
	Config::SetDefault("ns3::QbbNetDevice::QcnEnabled", BooleanValue(enable_qcn));
	Config::SetDefault("ns3::QbbNetDevice::DynamicThreshold", BooleanValue(dynamicth));

	// set int_multi
	IntHop::multi = int_multi;
	// IntHeader::mode
	if (cc_mode == 7) // timely, use ts
		IntHeader::mode = IntHeader::TS;
	else if (cc_mode == 3) // hpcc, use int
		IntHeader::mode = IntHeader::NORMAL;
	else if (cc_mode == 10) // hpcc-pint
		IntHeader::mode = IntHeader::PINT;
	else // others, no extra header
		IntHeader::mode = IntHeader::NONE;

	// Set Pint
	if (cc_mode == 10){
		Pint::set_log_base(pint_log_base);
		IntHeader::pint_bytes = Pint::get_n_bytes();
		printf("PINT bits: %d bytes: %d\n", Pint::get_n_bits(), Pint::get_n_bytes());
	}

	//SeedManager::SetSeed(time(NULL));

	topof.open(topology_file.c_str());
	uint32_t node_num, switch_num, link_num, trace_num = 0;
	topof >> node_num >> switch_num >> link_num;
	if (enable_trace)
	{
		tracef.open(trace_file.c_str());
		tracef >> trace_num;
	}


	//n.Create(node_num);
	std::vector<uint32_t> node_type(node_num, 0);
	for (uint32_t i = 0; i < switch_num; i++)
	{
		uint32_t sid;
		topof >> sid;
		node_type[sid] = 1;
	}
	for (uint32_t i = 0; i < node_num; i++){
		if (node_type[i] == 0)
			n.Add(CreateObject<Node>());
		else{
			Ptr<SwitchNode> sw = CreateObject<SwitchNode>();
			n.Add(sw);
			sw->SetAttribute("EcnEnabled", BooleanValue(enable_qcn));
		}
	}


	NS_LOG_INFO("Create nodes.");

	InternetStackHelper internet;
	internet.Install(n);

	//
	// Assign IP to each server
	//
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0){ // is server
			serverAddress.resize(i + 1);
			serverAddress[i] = node_id_to_ip(i);
		}
	}

	NS_LOG_INFO("Create channels.");

	//
	// Explicitly create the channels required by the topology.
	//

	Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
	Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
	rem->SetRandomVariable(uv);
	uv->SetStream(50);
	rem->SetAttribute("ErrorRate", DoubleValue(error_rate_per_link));
	rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

	FILE *pfc_file = fopen(pfc_output_file.c_str(), "w");

	QbbHelper qbb;
	Ipv4AddressHelper ipv4;
	for (uint32_t i = 0; i < link_num; i++)
	{
		uint32_t src, dst;
		std::string data_rate, link_delay;
		double error_rate;
		topof >> src >> dst >> data_rate >> link_delay >> error_rate;

		Ptr<Node> snode = n.Get(src), dnode = n.Get(dst);

		qbb.SetDeviceAttribute("DataRate", StringValue(data_rate));
		qbb.SetChannelAttribute("Delay", StringValue(link_delay));

		if (error_rate > 0)
		{
			Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
			Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
			rem->SetRandomVariable(uv);
			uv->SetStream(50);
			rem->SetAttribute("ErrorRate", DoubleValue(error_rate));
			rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
			qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
		}
		else
		{
			qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
		}

		fflush(stdout);

		// Assigne server IP
		// Note: this should be before the automatic assignment below (ipv4.Assign(d)),
		// because we want our IP to be the primary IP (first in the IP address list),
		// so that the global routing is based on our IP
		NetDeviceContainer d = qbb.Install(snode, dnode);
		if (snode->GetNodeType() == 0){
			Ptr<Ipv4> ipv4 = snode->GetObject<Ipv4>();
			ipv4->AddInterface(d.Get(0));
			ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[src], Ipv4Mask(0xff000000)));
		}
		if (dnode->GetNodeType() == 0){
			Ptr<Ipv4> ipv4 = dnode->GetObject<Ipv4>();
			ipv4->AddInterface(d.Get(1));
			ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[dst], Ipv4Mask(0xff000000)));
		}

		// used to create a graph of the topology
		nbr2if[snode][dnode].idx = DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex();
		nbr2if[snode][dnode].up = true;
		nbr2if[snode][dnode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(0))->GetChannel())->GetDelay().GetTimeStep();
		nbr2if[snode][dnode].bw = DynamicCast<QbbNetDevice>(d.Get(0))->GetDataRate().GetBitRate();
		nbr2if[dnode][snode].idx = DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex();
		nbr2if[dnode][snode].up = true;
		nbr2if[dnode][snode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(1))->GetChannel())->GetDelay().GetTimeStep();
		nbr2if[dnode][snode].bw = DynamicCast<QbbNetDevice>(d.Get(1))->GetDataRate().GetBitRate();

		// This is just to set up the connectivity between nodes. The IP addresses are useless
		char ipstring[16];
		sprintf(ipstring, "10.%d.%d.0", i / 254 + 1, i % 254 + 1);
		ipv4.SetBase(ipstring, "255.255.255.0");
		ipv4.Assign(d);

		// setup PFC trace
		DynamicCast<QbbNetDevice>(d.Get(0))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback (&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(0))));
		DynamicCast<QbbNetDevice>(d.Get(1))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback (&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(1))));
	}

	nic_rate = get_nic_rate(n);
	
	// config switch
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 1){ // is switch
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
			uint32_t shift = 3; // by default 1/8
			for (uint32_t j = 1; j < sw->GetNDevices(); j++){
				Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(sw->GetDevice(j));
				// set ecn
				uint64_t rate = dev->GetDataRate().GetBitRate();
				NS_ASSERT_MSG(rate2kmin.find(rate) != rate2kmin.end(), "must set kmin for each link speed");
				NS_ASSERT_MSG(rate2kmax.find(rate) != rate2kmax.end(), "must set kmax for each link speed");
				NS_ASSERT_MSG(rate2pmax.find(rate) != rate2pmax.end(), "must set pmax for each link speed");
				sw->m_mmu->ConfigEcn(j, rate2kmin[rate], rate2kmax[rate], rate2pmax[rate]);
				// set pfc
				uint64_t delay = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetTimeStep();
				uint32_t headroom = rate * delay / 8 / 1000000000 * 3;
				sw->m_mmu->ConfigHdrm(j, headroom);

				// set pfc alpha, proportional to link bw
				sw->m_mmu->pfc_a_shift[j] = shift;
				while (rate > nic_rate && sw->m_mmu->pfc_a_shift[j] > 0){
					sw->m_mmu->pfc_a_shift[j]--;
					rate /= 2;
				}
			}
			sw->m_mmu->ConfigNPort(sw->GetNDevices()-1);
			if (i < node_num - 16)
				sw->m_mmu->ConfigBufferSize(buffer_size* 1024 * 1024);
			else 
				sw->m_mmu->ConfigBufferSize(buffer_size / 2 * 1024 * 1024);
			sw->m_mmu->node_id = sw->GetId();
		}
	}
	SwitchNode::rps_seed = rps_seed;

	FILE *fct_output = fopen(fct_output_file.c_str(), "w");
	//
	// install RDMA driver
	//
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0){ // is server
			// create RdmaHw
			Ptr<RdmaHw> rdmaHw = CreateObject<RdmaHw>();
			rdmaHw->SetAttribute("ClampTargetRate", BooleanValue(clamp_target_rate));
			rdmaHw->SetAttribute("AlphaResumInterval", DoubleValue(alpha_resume_interval));
			rdmaHw->SetAttribute("RPTimer", DoubleValue(rp_timer));
			rdmaHw->SetAttribute("FastRecoveryTimes", UintegerValue(fast_recovery_times));
			rdmaHw->SetAttribute("EwmaGain", DoubleValue(ewma_gain));
			rdmaHw->SetAttribute("RateAI", DataRateValue(DataRate(rate_ai)));
			rdmaHw->SetAttribute("RateHAI", DataRateValue(DataRate(rate_hai)));
			rdmaHw->SetAttribute("L2BackToZero", BooleanValue(l2_back_to_zero));
			rdmaHw->SetAttribute("L2ChunkSize", UintegerValue(l2_chunk_size));
			rdmaHw->SetAttribute("L2AckInterval", UintegerValue(l2_ack_interval));
			rdmaHw->SetAttribute("CcMode", UintegerValue(cc_mode));
			rdmaHw->SetAttribute("RateDecreaseInterval", DoubleValue(rate_decrease_interval));
			rdmaHw->SetAttribute("MinRate", DataRateValue(DataRate(min_rate)));
			rdmaHw->SetAttribute("Mtu", UintegerValue(packet_payload_size));
			rdmaHw->SetAttribute("MiThresh", UintegerValue(mi_thresh));
			rdmaHw->SetAttribute("VarWin", BooleanValue(var_win));
			rdmaHw->SetAttribute("FastReact", BooleanValue(fast_react));
			rdmaHw->SetAttribute("MultiRate", BooleanValue(multi_rate));
			rdmaHw->SetAttribute("SampleFeedback", BooleanValue(sample_feedback));
			rdmaHw->SetAttribute("TargetUtil", DoubleValue(u_target));
			rdmaHw->SetAttribute("RateBound", BooleanValue(rate_bound));
			rdmaHw->SetAttribute("DctcpRateAI", DataRateValue(DataRate(dctcp_rate_ai)));
			rdmaHw->SetPintSmplThresh(pint_prob);
			// create and install RdmaDriver
			Ptr<RdmaDriver> rdma = CreateObject<RdmaDriver>();
			Ptr<Node> node = n.Get(i);
			rdma->SetNode(node);
			rdma->SetRdmaHw(rdmaHw);

			node->AggregateObject (rdma);
			rdma->Init();
			rdma->TraceConnectWithoutContext("QpComplete", MakeBoundCallback (qp_finish, fct_output));
		}
	}
	RdmaHw::m_ResponseHT = response_HT;
	// set ACK priority on hosts
	if (ack_high_prio)
		RdmaEgressQueue::ack_q_idx = 0;
	else
		RdmaEgressQueue::ack_q_idx = 3;

	// setup routing
	CalculateRoutes(n);
	SetRoutingEntries();

	//
	// get BDP and delay
	//
	maxRtt = maxBdp = 0;
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() != 0)
			continue;
		for (uint32_t j = 0; j < node_num; j++){
			if (n.Get(j)->GetNodeType() != 0)
				continue;
			uint64_t delay = pairDelay[n.Get(i)][n.Get(j)];
			uint64_t txDelay = pairTxDelay[n.Get(i)][n.Get(j)];
			uint64_t rtt = delay * 2 + txDelay;
			uint64_t bw = pairBw[i][j];
			uint64_t bdp = rtt * bw / 1000000000/8; 
			pairBdp[n.Get(i)][n.Get(j)] = bdp;
			pairRtt[i][j] = rtt;
			if (bdp > maxBdp)
				maxBdp = bdp;
			if (rtt > maxRtt)
				maxRtt = rtt;
		}
	}
	printf("maxRtt=%lu maxBdp=%lu\n", maxRtt, maxBdp);
	RdmaRxQueuePair::bitmapSize = maxBdp / (packet_payload_size + HEADER_SIZE) + 1;
	fprintf(stderr, "bitmap size: %d\n", RdmaRxQueuePair::bitmapSize);
	//
	// setup switch CC
	//
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 1){ // switch
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
			sw->SetAttribute("CcMode", UintegerValue(cc_mode));
			sw->SetAttribute("MaxRtt", UintegerValue(maxRtt));
			sw->SetAttribute("EnablePfc", BooleanValue(enable_pfc));
			sw->SetAttribute("RouteMode", UintegerValue(route_mode));
		}
	}

	//
	// add trace
	//

	NodeContainer trace_nodes;
	for (uint32_t i = 0; i < trace_num; i++)
	{
		uint32_t nid;
		tracef >> nid;
		if (nid >= n.GetN()){
			continue;
		}
		trace_nodes = NodeContainer(trace_nodes, n.Get(nid));
	}

	FILE *trace_output = NULL;
	if (enable_trace) {
		trace_output = fopen(trace_output_file.c_str(), "w");
		qbb.EnableTracing(trace_output, trace_nodes);
		
		// dump link speed to trace file
		SimSetting sim_setting;
		for (auto i: nbr2if){
			for (auto j : i.second){
				uint16_t node = i.first->GetId();
				uint8_t intf = j.second.idx;
				uint64_t bps = DynamicCast<QbbNetDevice>(i.first->GetDevice(j.second.idx))->GetDataRate().GetBitRate();
				sim_setting.port_speed[node][intf] = bps;
			}
		}
		sim_setting.win = maxBdp;
		sim_setting.Serialize(trace_output);
	}

	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	NS_LOG_INFO("Create Applications.");

	Time interPacketInterval = Seconds(0.0000005 / 2);

	// maintain port number for each host
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0)
			for (uint32_t j = 0; j < node_num; j++){
				if (n.Get(j)->GetNodeType() == 0)
					portNumder[i][j] = 10000; // each host pair use port number from 10000
			}
	}

	// create alltoall jobs according to group_size, group_num, intra_group_stride, inter_group_stride
	vector<AlltoallJob*> alltoall_jobs;
	for (uint32_t group_id = 0; group_id < group_num; group_id++) {
		// Create node list for this group
		std::vector<uint32_t> group_nodes;
		for (uint32_t i = 0; i < group_size; i++) {
			// Calculate node index based on group_id and intra_group_stride
			uint32_t node_id = (group_id * inter_group_stride + i * intra_group_stride) % (node_num - switch_num);
			// Ensure node_id is within valid range
			group_nodes.push_back(node_id);
		}

		// Create job with default parameters
		uint32_t job_id = group_id;
		uint32_t tensor_size = alltoall_message_size;
		double start_time = 2.0;
		AlltoallJob* job = new AlltoallJob(job_id, group_nodes, tensor_size, start_time);
		
		// Store job
		alltoall_jobs.push_back(job);
		
		// Print job info
		std::cout << "Created AlltoallJob " << job_id << " with nodes: ";
		for (auto node : group_nodes) {
			std::cout << node << " ";
		}
		std::cout << std::endl;
	}

	// Start all jobs
	for (auto job : alltoall_jobs) {
		job->ScheduleStartEvent();
	}

	topof.close();
	if (enable_trace)
		tracef.close();
	// schedule link down
	if (link_down_time > 0){
		Simulator::Schedule(Seconds(2) + MicroSeconds(link_down_time), &TakeDownLink, n, n.Get(link_down_A), n.Get(link_down_B));
	}

	//
	// Now, do the actual simulation.
	//
	std::cout << "Running Simulation.\n";
	fflush(stdout);
	NS_LOG_INFO("Run Simulation.");
	Simulator::Stop(Seconds(simulator_stop_time));
	Simulator::Run();
	printf("Simulation finished at %lf\n", Simulator::Now().GetSeconds());
	Simulator::Destroy();
	NS_LOG_INFO("Done.");
	if (enable_trace && trace_output != NULL)
		fclose(trace_output);

	endt = clock();
	
	// write alltoall statics to alltoall_output_file
	FILE* alltoall_output = fopen(alltoall_output_file.c_str(), "w");
	for (auto job : alltoall_jobs) {
		job->write_statics(alltoall_output);
	}
	fclose(alltoall_output);

	std::cout << (double)(endt - begint) / CLOCKS_PER_SEC << "\n";
}
