# DCP simulation
This repository contains the simulator for DCP:

+ [Revisiting RDMA Reliability for Lossy Fabrics (SIGCOMM'25)](https://dl.acm.org/doi/10.1145/3718958.3750480).

The simulator is built upon the [HPCC simulator](https://github.com/alibaba-edu/High-Precision-Congestion-Control). 

We implement the packet trimming mechanism and the logical RNIC functionality in this simulator. Hardware-specific logic, such as batched retransmission fetching and bitmap-free packet tracking, is not implemented here. Instead, these components are implemented on FPGA.

In addition, we provide an MP-RDMA implementation (see the `mprdma` branch).

# Quick start
## Run DCP
```bash
cd simulation

# Build
CC='gcc-5' CXX='g++-5' ./waf configure

# Run DCP experiments
./waf --run 'scratch/ring-allreduce exp/allreduce/config_allreduce.txt'
./waf --run 'scratch/alltoall exp/alltoall/config_alltoall.txt' 
```
taking allreduce as an example, after the simulation finishes, you will find
`dcp_ar_allreduce.txt` and `dcp_ar_fct.txt` under the directory `simulation/exp/allreduce`.

### Output format of dcp_ar_allreduce.txt:

This file records completion statistics for each Ring-Allreduce job.  
Each line corresponds to one job with the following syntax:

| Syntax | Description |
|-------|-------------|
| `job_id` | Job ID |
| `job_start_time` | Job start time (nanoseconds, ns) |
| `total_time` | Actual job completion time (JCT, nanoseconds) |
| `standalone_jct` | Ideal standalone completion time (nanoseconds) |

Each line in the file is formatted as:

```
job_id  job_start_time  total_time  standalone_jct
```
### Output format of dcp_ar_fct.txt

This file records the Flow Completion Time (FCT) of each subflow.  
Each line corresponds to one subflow with the following syntax:

| Syntax | Description |
|-------|-------------|
| `src_id` | Source node ID |
| `dst_id` | Destination node ID |
| `src_port` | Source port number |
| `dst_port` | Destination port number |
| `flow_size` | Flow size (bytes) |
| `start_time` | Flow start time (nanoseconds, ns) |
| `fct` | Flow completion time (FCT, nanoseconds) |
| `standalone_fct` | Ideal standalone completion time (nanoseconds) |
| `retx_bytes` | Number of retransmitted bytes |
| `timeout_count` | Number of timeouts |
| `qp_id` | Queue Pair (QP) ID |

Each line in the file is formatted as:
```
src_id  dst_id  src_port  dst_port  flow_size  start_time  fct  standalone_fct  retx_bytes  timeout_count  qp_id
```

## Run MP-RDMA
```
./waf --run 'scratch/ring-allreduce exp/allreduce/config_allreduce.txt'
./waf --run 'scratch/alltoall exp/alltoall/config_alltoall.txt'
```
The output file formats are similar to those of DCP.

# Simulation
The src code is under `simulation/`

# Traffic generator
The traffic generator is under `traffic_gen/`. 

# Analysis
We provide a few analysis scripts under `analysis/`.

# Citation
Please consider citing this work if you find the repository helpful.
```bibtex
@inproceedings{li2025revisiting,
  title={Revisiting RDMA Reliability for Lossy Fabrics},
  author={Li, Wenxue and Liu, Xiangzhou and Zhang, Yunxuan and Wang, Zihao and Gu, Wei and Qian, Tao and Zeng, Gaoxiong and Ren, Shoushou and Huang, Xinyang and Ren, Zhenghang and others},
  booktitle={Proceedings of the ACM SIGCOMM 2025 Conference},
  pages={85--98},
  year={2025}
}
```