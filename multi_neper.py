#!/usr/bin/env python3

import argparse, sys, os, subprocess
from logging import debug,info,warning,error,critical,basicConfig

parser=argparse.ArgumentParser()

link_to_gpu_pci_addr = {
        "eth1": "0000:04:00.0", # GPU0
        "eth2": "0000:0a:00.0", # GPU2
        "eth3": "0000:84:00.0", # GPU4
        "eth4": "0000:8a:00.0"  # GPU6
}

link_to_nic_pci_addr = {
        "eth1": "0000:06:00.0",
        "eth2": "0000:0c:00.0",
        "eth3": "0000:86:00.0",
        "eth4": "0000:8c:00.0"
}

link_to_gpu_index = {
        "eth1": "0",
        "eth2": "2",
        "eth3": "4",
        "eth4": "6"
}

# returns a 2-tuple of a Neper command and a dict of env vars
def build_neper_cmd(neper_dir: str, is_client: bool, dev: str,
                    threads: int, flows: int,
                    cpu_list, buffer_size: int, phys_len: int,
                    nic_pci: str, gpu_pci: str,
                    control_port, source_port, port, length,
                    src_ip, dst_ip, queue_start, queue_num,
                    tcpd_validate, tcpd_rx_cpy)->tuple:

        cmd = (f"taskset --cpu-list {cpu_list} {neper_dir}/tcp_stream"
               f" -T {threads} -F {flows}"
               f" --port {port} --source-port {source_port}"
               f" --control-port {control_port}"
               f" --buffer-size {buffer_size} "
               f" -l {length}"
               f" --num-ports {flows}")

        if phys_len:
                cmd += f" --tcpd-phys-len {phys_len}"
        if nic_pci:
                cmd += f" --tcpd-nic-pci-addr {nic_pci}"
        if gpu_pci:
                cmd += f" --tcpd-gpu-pci-addr {gpu_pci}"
        if tcpd_validate:
                cmd += " --tcpd-validate"

        if is_client:
                cmd += f" -c -H {dst_ip}"
        else:
                cmd = cmd + (f" --tcpd-link-name {dev}"
                             f" --tcpd-src-ip {src_ip}"
                             f" --tcpd-dst-ip {dst_ip}"
                             f" --queue-start {queue_start}"
                             f" --queue-num {queue_num}")
                if tcpd_rx_cpy:
                        cmd += " --tcpd-rx-cpy"

        env = {"CUDA_VISIBLE_DEVICES": link_to_gpu_index[dev]}
        env.update(os.environ.copy())

        return (cmd, env)

# returns a CPU range for taskset
# e.x. returns 4-7 provided 0, 4, 1 as arguments
def get_cpu_range(starting_cpu:int, interval: int, idx: int)->str:
        cpu_start = idx * interval + starting_cpu
        cpu_end = cpu_start + interval - 1
        return f"{cpu_start}-{cpu_end}"

def run_cmds(cmds: list)->list:
        sp_list = []
        for cmd, env in cmds:
                popen = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
                sp_list.append(popen)

        return sp_list

def parse_subprocess_outputs(subprocesses):
        output_dicts = []

        for sp in subprocesses:
                cur_hash = dict()

                sp.wait()
                debug(sp.stderr.read())
                for line in sp.stdout.read().split("\n"):
                        debug(line)
                        stripped_line = line.strip()
                        if "=" in stripped_line:
                                parsed_line = stripped_line.split("=")
                                cur_hash[parsed_line[0]] = parsed_line[1]
                if cur_hash:
                        output_dicts.append(cur_hash)

        return output_dicts

if __name__ == "__main__":
        parser = argparse.ArgumentParser()

        parser.add_argument("--neper-dir", help="directory containing Neper binaries", default=".")
        parser.add_argument("--threads", help="number of threads per Neper instance", default="4", type=int)
        parser.add_argument("--flows", help="number of flows per Neper instance", default="4", type=int)
        parser.add_argument("--source-port", default="12345", type=int)
        parser.add_argument("--port", default="12345", type=int)
        parser.add_argument("--control-port", default="12866", type=int)
        parser.add_argument("--devices", help="comma-delimited list of links to run Neper on, i.e. eth1,eth2,eth3",
                            default="eth1")
        parser.add_argument("--phys-len", default=4294967296)
        parser.add_argument("--buffer-size", default=4096*120)

        parser.add_argument("-c", "--client", action="store_true")
        parser.add_argument("--src-ips", required="--client" not in sys.argv and "-c" not in sys.argv,
                            help="required for Host to install/remove flow-steering rules, comma-delimited list of client IP addresses")
        parser.add_argument("-H", "--hosts", required=True,
                            help="comma-delimited list of host IP addresses")

        parser.add_argument("--q-start", default="8", help="starting queue for flow-steering rules", type=int)
        parser.add_argument("--q-num", default="4", help=("number of queues for flow-steering rules"
                                                          " (i.e. if q-start=8 and q-num=4, 2"
                                                          " flow-steering rules each will be"
                                                          " installed for queues [8-11])"),
                                                          type=int)
        parser.add_argument("--tcpd-validate", action="store_true")
        parser.add_argument("--tcpd-rx-cpy", action="store_true")

        parser.add_argument("--dry-run", default=False, action="store_true")

        parser.add_argument("-l", "--length", default=10)
        parser.add_argument("--log", default="WARNING")
        parser.add_argument("-m", "--mode", default="cuda", help="cuda|udma|default")

        args = parser.parse_args()

        basicConfig(level=args.log.upper())

        devices = args.devices.split(",")
        hosts = args.hosts.split(",")
        src_ips = args.src_ips.split(",")

        dev_to_rule = dict()
        # setup flow_steering rules
        if not args.client:
                info("setting up flow-steering rules")
                # src_ips = args.src_ips.split(",")

        cmds = []
        debug(f"running on {devices}")
        is_client = args.client

        for i, dev in enumerate(devices):
                nic_pci, gpu_pci = None, None

                if args.mode.lower() in ["cuda", "udma"]:
                        nic_pci = link_to_nic_pci_addr[dev]
                if args.mode.lower() == "cuda":
                        gpu_pci = link_to_gpu_pci_addr[dev]

                # increment control port by 1, and src/dst ports by flow_count
                # for each additional link we're running Neper on
                ctrl_port = args.control_port + i
                src_port = i * args.flows + args.source_port
                dst_port = i * args.flows + args.port

                src_ip, dst_ip = src_ips[i], hosts[i]

                # TODO should CPU range be configurable by the user?
                cpu_range = get_cpu_range(2 + (52 if i >= 2 else 0), args.threads, i)

                cmd_env = build_neper_cmd(args.neper_dir, is_client, dev,
                                      args.threads, args.flows, cpu_range, args.buffer_size,
                                      args.phys_len, nic_pci, gpu_pci,
                                      ctrl_port, src_port, dst_port, args.length, src_ip, dst_ip,
                                      args.q_start, args.q_num, args.tcpd_validate, args.tcpd_rx_cpy)

                cmds.append(cmd_env)

        for cmd in cmds:
                debug(cmd)

        if not args.dry_run:
                sp_list = run_cmds(cmds)
                debug("parsing subprocesses outputs")
                for dev, i in zip(devices, parse_subprocess_outputs(sp_list)):
                        if not args.client:
                                try:
                                        print(f"[{dev}] Throughput (Mb/s): {i['throughput']}")
                                except KeyError:
                                        print(f"[{dev}] Throughput (Mb/s): NA")
