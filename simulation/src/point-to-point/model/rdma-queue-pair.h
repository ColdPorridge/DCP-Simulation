#ifndef RDMA_QUEUE_PAIR_H
#define RDMA_QUEUE_PAIR_H

#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/ipv4-address.h>
#include <ns3/data-rate.h>
#include <ns3/event-id.h>
#include <ns3/custom-header.h>
#include <ns3/int-header.h>
#include <vector>

namespace ns3 {

enum class QpState {
	normal,
	initial,
	recovery
};

class RdmaQueuePair : public Object {
public:
	static Time m_rto;
	Time startTime;
	uint32_t m_qpId;
	Ipv4Address sip, dip;
	uint16_t sport, dport;
	uint64_t m_size;
	uint64_t snd_nxt, snd_una; // next seq to send, the highest unacked seq
	uint16_t m_pg;
	uint16_t m_ipid;
	uint64_t m_baseRtt; // base RTT of this qp
	DataRate m_max_rate; // max rate
	bool m_var_win; // variable window size
	Time m_nextAvail;	//< Soonest time of next send
	uint32_t wp; // current window of packets
	uint32_t lastPktSize;
	Callback<void> m_notifyAppFinish;
	uint64_t m_mtu;
	uint64_t m_retxNum = 0;
	uint64_t m_timeOutNum = 0;
	/******************************
	 * runtime states for MPRDMA
	 *****************************/
	QpState m_currentState;
	uint16_t m_currentVp;
	double m_initialWin;
	double m_win; // bound of on-the-fly packets
	uint32_t m_inflight;
	EventId m_eventTimeout;
	uint64_t snd_retx; // next seq to retransmit, used in recovery state
	bool m_start = false;
	uint64_t snd_ooh;
	/******************************
	 * runtime states
	 *****************************/
	DataRate m_rate;	//< Current rate
	struct {
		DataRate m_targetRate;	//< Target rate
		EventId m_eventUpdateAlpha;
		double m_alpha;
		bool m_alpha_cnp_arrived; // indicate if CNP arrived in the last slot
		bool m_first_cnp; // indicate if the current CNP is the first CNP
		EventId m_eventDecreaseRate;
		bool m_decrease_cnp_arrived; // indicate if CNP arrived in the last slot
		uint32_t m_rpTimeStage;
		EventId m_rpTimer;
	} mlx;
	struct {
		uint32_t m_lastUpdateSeq;
		DataRate m_curRate;
		IntHop hop[IntHeader::maxHop];
		uint32_t keep[IntHeader::maxHop];
		uint32_t m_incStage;
		double m_lastGap;
		double u;
		struct {
			double u;
			DataRate Rc;
			uint32_t incStage;
		}hopState[IntHeader::maxHop];
	} hp;
	struct{
		uint32_t m_lastUpdateSeq;
		DataRate m_curRate;
		uint32_t m_incStage;
		uint64_t lastRtt;
		double rttDiff;
	} tmly;
	struct{
		uint32_t m_lastUpdateSeq;
		uint32_t m_caState;
		uint32_t m_highSeq; // when to exit cwr
		double m_alpha;
		uint32_t m_ecnCnt;
		uint32_t m_batchSizeOfAlpha;
	} dctcp;
	struct{
		uint32_t m_lastUpdateSeq;
		DataRate m_curRate;
		uint32_t m_incStage;
	}hpccPint;

	/***********
	 * methods
	 **********/
	static TypeId GetTypeId (void);
	RdmaQueuePair(uint16_t pg, Ipv4Address _sip, Ipv4Address _dip, uint16_t _sport, uint16_t _dport, uint32_t qp_id);
	void SetSize(uint64_t size);
	void SetWin(uint32_t win);
	void SetBaseRtt(uint64_t baseRtt);
	void SetMtu(uint64_t mtu);
	void SetVarWin(bool v);
	void SetAppNotifyCallback(Callback<void> notifyAppFinish);

	uint64_t GetBytesLeft();
	bool HasPacket();
	uint32_t GetHash(void);
	void Acknowledge(uint64_t ack);
	uint64_t GetOnTheFly();
	bool IsWinBound();
	uint64_t GetWin(); // window size calculated from m_rate
	uint32_t GetSeqPktSize(uint32_t seq);
	bool IsFinished();
	uint64_t HpGetCurWin(); // window size calculated from hp.m_curRate, used by HPCC
};

class RingBitmap : public Object {
public:
    RingBitmap(size_t len) : len(len), head(0), bitmap(len, 0) {}
    bool operator[](size_t i) const {
		NS_ASSERT_MSG(i < len, "RingBitmap::operator[]: i >= len");
        return bitmap[(head + i) % len];
    }
    // Write bit and update head
	// use return value to indicate if seq is in the bitmap range
	// use steps to indicate how many steps the head has moved
    bool writeBit(size_t i, size_t &steps) {
		if (i >= len)
			return false;

        size_t pos = (head + i) % len;
        bitmap[pos] = 1;

        steps = 0;
        while (bitmap[head] == 1) {
            bitmap[head] = 0;
            head = (head + 1) % len;
            steps++;
        }
        return true;
    }
private:
    size_t len;
    size_t head;
    std::vector<bool> bitmap;
};

class RdmaRxQueuePair : public Object { // Rx side queue pair
public:
	static uint32_t bitmapSize;
	struct ECNAccount{
		uint16_t qIndex;
		uint8_t ecnbits;
		uint16_t qfb;
		uint16_t total;

		ECNAccount() { memset(this, 0, sizeof(ECNAccount));}
	};
	ECNAccount m_ecn_source;
	uint32_t sip, dip;
	uint16_t sport, dport;
	uint16_t m_ipid;
	uint32_t ReceiverNextExpectedSeq;
	Time m_nackTimer;
	int32_t m_milestone_rx;
	uint32_t m_lastNACK;
	EventId QcnTimerEvent; // if destroy this rxQp, remember to cancel this timer

	Ptr<RingBitmap> m_bitmap;

	static TypeId GetTypeId (void);
	RdmaRxQueuePair();
	uint32_t GetHash(void);
};

class RdmaQueuePairGroup : public Object {
public:
	std::vector<Ptr<RdmaQueuePair> > m_qps;
	//std::vector<Ptr<RdmaRxQueuePair> > m_rxQps;

	static TypeId GetTypeId (void);
	RdmaQueuePairGroup(void);
	uint32_t GetN(void);
	Ptr<RdmaQueuePair> Get(uint32_t idx);
	Ptr<RdmaQueuePair> operator[](uint32_t idx);
	void AddQp(Ptr<RdmaQueuePair> qp);
	//void AddRxQp(Ptr<RdmaRxQueuePair> rxQp);
	void Clear(void);
};

}

#endif /* RDMA_QUEUE_PAIR_H */
