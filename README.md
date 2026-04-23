---

# Multi-Container Runtime (OS-Jackfruit)

---

## 1. Team Information

* Name: AMRUTHA KATTIMANI

* SRN: PES1UG24CS054

* Name: SAMHITHA

* SRN: PES1UG24CS050



---

## 2. Build, Load, and Run Instructions

### Setup (first time only)
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)


---

### Build the project
make


---

### Prepare base root filesystem
mkdir rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base


---

### Load kernel module
sudo insmod monitor.ko


Check if device is created:
ls -l /dev/container_monitor


---

### Start supervisor
sudo ./engine supervisor ./rootfs-base

---

### Create container rootfs copies
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta


---

### Start containers (run in another terminal)
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96


---

### Check running containers
sudo ./engine ps

---

### View logs
sudo ./engine logs alpha


---

### Running workloads

If you have a test program:
cp workload_binary ./rootfs-alpha/


Then run it using the `run` command or inside the container shell.

---

### Scheduling experiments

* Run two containers at the same time
* Use different `--nice` values
* Try CPU-heavy and I/O-heavy programs
* Observe differences in execution

---

### Stop containers
sudo ./engine stop alpha
sudo ./engine stop beta


---

### Stop supervisor

Press `Ctrl + C` in the supervisor terminal or kill the process.

---

### Check kernel logs
dmesg | tail

---

### Unload module
sudo rmmod monitor


---

## 3. Demo with Screenshots

| # | What to Demonstrate         | What the Screenshot Must Show                             |
| - | --------------------------- | --------------------------------------------------------- |
| 1 | Multi-container supervision | At least two containers running under the same supervisor |
<img width="1001" height="416" alt="1-multi-container-ter1" src="https://github.com/user-attachments/assets/b6d5a568-ad43-4dc4-8493-a338d71b20c8" />
<img width="1001" height="416" alt="1-multi-container-ter2" src="https://github.com/user-attachments/assets/df4d32ba-6ec0-49e8-a150-13999fae4cc5" />
| 2 | Metadata tracking           | Output of `engine ps` showing container details           |
<img width="988" height="119" alt="2-metadata-ps" src="https://github.com/user-attachments/assets/9beca45d-be9d-4d82-9b18-e2255aed4ee3" />
| 3 | Bounded-buffer logging      | Log file output showing captured logs                     |
<img width="983" height="246" alt="3-logging" src="https://github.com/user-attachments/assets/95c5dfc4-6311-4841-bb6f-e389f3b46bb3" />
| 4 | CLI and IPC                 | A command being sent and response from supervisor         |
<img width="1211" height="109" alt="4-cli-ipc" src="https://github.com/user-attachments/assets/ee7c9571-55ae-423a-84f6-9d29daf22ee3" />
| 5 | Soft-limit warning          | Warning message in `dmesg` or logs                        |
<img width="1077" height="58" alt="5-6-initial" src="https://github.com/user-attachments/assets/1d3148f8-d28d-413d-bff6-c5b10ed11e0b" />
<img width="1211" height="104" alt="5-soft-limit-ter1" src="https://github.com/user-attachments/assets/36123df8-152a-4592-9e08-930b5d1508d4" />
<img width="1211" height="111" alt="5-soft-limit-ter2" src="https://github.com/user-attachments/assets/f9afb541-e614-461e-bb96-b3b503249b59" />
| 6 | Hard-limit enforcement      | Container killed after exceeding limit                    |
<img width="1211" height="99" alt="6-hard-limit-ter1" src="https://github.com/user-attachments/assets/76c34601-6c0a-4f7f-a238-e633a2b60ec1" />
<img width="1156" height="86" alt="6-hard-limit-ter2" src="https://github.com/user-attachments/assets/e663f719-e232-45d2-b4e4-7cb1324e522f" />
| 7 | Scheduling experiment       | Difference in execution between workloads                 |
<img width="1211" height="62" alt="7-scheduler-ter1" src="https://github.com/user-attachments/assets/8f27af8c-11ef-46f9-9541-1a322f8e9a49" />
<img width="1211" height="180" alt="7-scheduler-ter2" src="https://github.com/user-attachments/assets/7c609252-ad7d-49d9-b254-f5267fef4ab1" />
<img width="1211" height="66" alt="7-scheduler-ter3" src="https://github.com/user-attachments/assets/21284980-ec89-4775-98b6-9a89df2c8b46" />
<img width="1211" height="140" alt="7-scheduler-ter4" src="https://github.com/user-attachments/assets/70a59d7a-c584-417d-b995-7831e26c8e4c" />
| 8 | Clean teardown              | No zombie processes after stopping everything             |
<img width="1211" height="137" alt="8-cleanup-ter1" src="https://github.com/user-attachments/assets/fe1924b6-e781-470b-bf55-7bd952e1174c" />
<img width="1211" height="166" alt="8-cleanup-ter2" src="https://github.com/user-attachments/assets/9ff279f6-b05b-475e-ad77-a43805ada0a3" />
<img width="1211" height="74" alt="8-cleanup-ter3" src="https://github.com/user-attachments/assets/2eaa9247-0efc-4784-8fc5-1b2e6bf6b372" />

---

## 4. Engineering Analysis

### Isolation Mechanisms

Each container runs in its own environment using namespaces.
PID namespace separates process IDs, UTS changes hostname, and mount namespace isolates filesystem.
`chroot()` is used so each container only sees its own root filesystem.
Even though containers are isolated, they still share the same Linux kernel.

---

### Supervisor and Process Lifecycle

Instead of launching containers directly, everything goes through a supervisor.
This makes it easier to manage multiple containers at once.
The supervisor keeps track of each container and handles signals properly.
It also makes sure no zombie processes are left behind.

---

### IPC, Threads, and Synchronization

Two types of communication are used:

* Pipes for logs (container → supervisor)
* Socket/FIFO for commands (CLI → supervisor)

Since multiple threads are involved, synchronization is needed.
Mutex locks are used to protect shared data, and condition variables help manage the buffer.

Without this, logs could get mixed up or the system could hang.

---

### Memory Management and Enforcement

Memory usage is tracked using RSS (resident set size).

Soft limit just gives a warning when exceeded.
Hard limit forcefully kills the container.

This is handled in kernel space because user programs cannot reliably control memory usage.

---

### Scheduling Behavior

Experiments showed how Linux scheduling works in practice.

Processes with higher priority (lower nice value) got more CPU time.
I/O-bound programs stayed responsive, while CPU-heavy ones used most of the CPU.

This matches how the Linux scheduler is designed.

---

## 5. Design Decisions and Tradeoffs

| Component      | Choice         | Tradeoff                    | Reason               |
| -------------- | -------------- | --------------------------- | -------------------- |
| Isolation      | chroot()       | Not as secure as pivot_root | Easier to implement  |
| Supervisor     | Single process | Central dependency          | Simpler control      |
| IPC            | Pipes + socket | Slight overhead             | Works reliably       |
| Logging        | Bounded buffer | More code complexity        | Prevents data loss   |
| Kernel Monitor | LKM            | Needs root privileges       | Better control       |
| Scheduling     | nice values    | Limited tuning              | Simple and effective |

---

## 6. Scheduler Experiment Results

### Experiment 1: Two CPU-bound processes

| Container | Nice Value | Result          |
| --------- | ---------- | --------------- |
| A         | 0          | Finished faster |
| B         | 10         | Took longer     |

This shows higher priority processes get more CPU time.

---

### Experiment 2: CPU-bound vs I/O-bound

| Type      | Observation      |
| --------- | ---------------- |
| CPU-bound | Uses most CPU    |
| I/O-bound | Responds quickly |

I/O-bound tasks get scheduled sooner after waiting, so they feel more responsive.

---

### Final Observation

The Linux scheduler balances fairness and responsiveness.
It does not give equal time blindly — it adjusts based on process behavior.

---

