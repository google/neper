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

# adds flow-steering rules, e.x.
# ethtool -N eth1 flow-type tcp4 ...
def install_flow_steer_rules(dev, threads: int, src_port, port, src_ip, dst_ip)->list:
        subprocesses, rules = [], []

        for i in range(threads):
                flow_steering_cmd = f"ethtool -N {dev} flow-type tcp4 src-ip {src_ip} dst-ip {dst_ip} src-port {src_port + i} dst-port {port} queue 15"
                sp = subprocess.run(flow_steering_cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                subprocesses.append(sp)

                line = sp.stdout.strip()
                # the expected output will be similar to:
                # "Added rule with ID 19989"
                if "Added rule with ID" in line:
                        rule = line.split()[-1]
                        debug(f"[{dev}] added rule {rule}: {src_ip} {dst_ip} {src_port + i} {port}")
                        rules.append(rule)

        return rules


# deletes flow-steering rules, given a list of rules and a link name
def del_flow_steer_rules(dev: str, rules: list):
        for rule in rules:
                del_cmd = f"ethtool -N {dev} delete {rule}"
                debug(f"[{dev}] deleting rule {rule}")
                subprocess.run(del_cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

# returns a 2-tuple of a Neper command and a dict of env vars
def build_neper_cmd(neper_dir: str, is_client: bool, dev: str,
                    threads: int, flows: int,
                    cpu_list, buffer_size: int, phys_len: int,
                    nic_pci: str, gpu_pci: str,
                    control_port, source_port, port, length, host_ip=None)->str:

        # TODO tcp_stream_cuda2 -> tcp_stream eventually
        cmd = (f"taskset --cpu-list {cpu_list} {neper_dir}/tcp_stream_cuda2 -T {threads} -F {flows} --tcpdirect-phys-len {phys_len}"
                f" --port {port} --source-port {source_port} --control-port {control_port} --tcpdirect-gpu-idx {link_to_gpu_index[dev]}"
                f" --buffer-size {buffer_size} --tcpd-nic-pci-addr {nic_pci} --tcpd-gpu-pci-addr {gpu_pci} -l {length}")

        env = None
        if is_client:
                cmd += f" -c -H {host_ip}"
        else:
                cmd = cmd + f" --tcpdirect-link-name {dev}"
                env = {"CUDA_VISIBLE_DEVICES": link_to_gpu_index[dev]}

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
                for line in sp.stdout.read().split("\n"):
                        stripped_line = line.strip()
                        if "=" in stripped_line:
                                parsed_line = stripped_line.split("=")
                                cur_hash[parsed_line[0]] = parsed_line[1]
                if cur_hash:
                        output_dicts.append(cur_hash)

        return output_dicts

if __name__ == "__main__":
        parser = argparse.ArgumentParser()

        parser.add_argument("--neper-dir", help="directory containing Neper binaries", default="/tmp/neper")
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

        parser.add_argument("-l", "--length", default=10)
        parser.add_argument("--log", default="WARNING")

        args = parser.parse_args()

        basicConfig(level=args.log.upper())

        devices = args.devices.split(",")
        hosts = args.hosts.split(",")

        dev_to_rule = dict()
        # setup flow_steering rules
        if not args.client:
                info("setting up flow-steering rules")
                src_ips = args.src_ips.split(",")

                for i in range(len(devices)):
                        control_port = args.control_port + i
                        starting_port = i * args.threads + args.source_port
                        dev = devices[i]
                        src_ip, dst_ip = src_ips[i], hosts[i]

                        rules = install_flow_steer_rules(dev, args.threads, starting_port, args.port, src_ip, dst_ip)
                        dev_to_rule[dev] = rules

        cmds = []
        debug(f"running on {devices}")
        for i, dev in enumerate(devices):
                nic_pci = link_to_nic_pci_addr[dev]
                gpu_pci = link_to_gpu_pci_addr[dev]

                ctrl_port = int(args.control_port) + i
                src_port = int(args.source_port) + i*int(args.flows)
                is_client = args.client
                host_ip = hosts[i] if is_client else None
                cpu_range = get_cpu_range(2, 3, i)

                cmd_env = build_neper_cmd(args.neper_dir, is_client, dev,
                                      args.threads, args.flows, cpu_range, args.buffer_size,
                                      args.phys_len, nic_pci, gpu_pci,
                                      ctrl_port, src_port, args.port, args.length, host_ip)

                cmds.append(cmd_env)

        debug(cmds)
        sp_list = run_cmds(cmds)
        debug("parsing subprocesses outputs")
        for dev, i in zip(devices, parse_subprocess_outputs(sp_list)):
                if not args.client:
                        print(f"[{dev}] Throughput (Mb/s): {i['throughput']}")

        # delete flow-steering rules
        if not args.client:
                info("deleting flow-steering rules")
                for dev in dev_to_rule:
                        del_flow_steer_rules(dev, dev_to_rule[dev])
