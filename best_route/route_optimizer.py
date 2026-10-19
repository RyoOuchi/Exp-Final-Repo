"""Choose routes and split a file using the model T = RTT + data / rate.

Run from the project folder:
    .venv/bin/python best_route/route_optimizer.py

The defaults are node1 -> node4 and a 10 MB file (10,000,000 bytes).
Edit NETWORK_LINKS below to change the measurements. No CSV files are read.
This program calculates a transfer plan. It does not send any network traffic.
"""

import argparse
import math

from scipy.optimize import linprog


TIME_TOLERANCE_SECONDS = 0.000001


class Link:
    """A container for one direction's measurements; no algorithm lives here."""

    def __init__(self, start, end, bitrate_mbps, rtt_ms):
        self.start = start
        self.end = end
        self.bitrate_mbps = bitrate_mbps
        self.rtt_ms = rtt_ms


# Link(from_node, to_node, bitrate in Mbit/s, RTT in milliseconds)
# The supplied measurements use the same RTT for both directions of a pair.
# Bitrates stay separate: for example, 1 -> 4 is 26.3, but 4 -> 1 is 26.8.
NETWORK_LINKS = [
    Link(1, 2, 1.24, 0.312),
    Link(1, 3, 1.24, 10.243),
    Link(1, 4, 26.3, 500.000),
    Link(1, 5, 47.9, 0.059),
    Link(2, 1, 1.24, 0.312),
    Link(2, 3, 47.9, 0.082),
    Link(2, 4, 1.24, 10.245),
    Link(2, 5, 47.9, 0.083),
    Link(3, 1, 1.24, 10.243),
    Link(3, 2, 47.9, 0.082),
    Link(3, 4, 50.3, 10.428),
    Link(3, 5, 1.24, 10.376),
    Link(4, 1, 26.8, 500.000),
    Link(4, 2, 1.24, 10.245),
    Link(4, 3, 50.2, 10.428),
    Link(4, 5, 50.3, 10.492),
    Link(5, 1, 47.9, 0.059),
    Link(5, 2, 47.9, 0.083),
    Link(5, 3, 1.24, 10.376),
    Link(5, 4, 50.3, 10.492),
]


def build_network(links):
    """Make a lookup table: network[(1, 4)] gives that direction's Link."""
    network = {}
    for link in links:
        if link.start == link.end:
            raise ValueError("A link must connect different nodes")
        if not math.isfinite(link.bitrate_mbps) or link.bitrate_mbps <= 0:
            raise ValueError("Bitrate must be finite and greater than zero")
        if not math.isfinite(link.rtt_ms) or link.rtt_ms < 0:
            raise ValueError("RTT must be finite and nonnegative")

        direction = (link.start, link.end)
        if direction in network:
            raise ValueError(f"Duplicate directed link: {direction}")
        network[direction] = link

    if not network:
        raise ValueError("At least one link is required")
    return network


# STEP 1: Generate candidate routes using DFS.
def find_all_paths(network, source, destination):
    """Use depth-first search (DFS) to find paths with no repeated nodes."""
    neighbors = {}
    for start, end in sorted(network):
        if start not in neighbors:
            neighbors[start] = []
        if end not in neighbors:
            neighbors[end] = []
        neighbors[start].append(end)

    if source == destination:
        raise ValueError("Source and destination must be different")
    if source not in neighbors or destination not in neighbors:
        raise ValueError("Source or destination is not in the network")

    paths = []

    def visit(path):
        current_node = path[-1]
        if current_node == destination:
            paths.append(path)
            return

        for next_node in neighbors[current_node]:
            # Skipping visited nodes prevents routes such as 1 -> 2 -> 1.
            if next_node not in path:
                visit(path + [next_node])

    visit([source])
    if not paths:
        raise ValueError(f"No route exists from node{source} to node{destination}")
    return paths


# STEP 2: Record each route's links and calculate its RTT.
def describe_routes(paths, network):
    """Calculate the links, bottleneck bitrate, and RTT of each path."""
    routes = []
    for path in paths:
        links = []
        link_bitrates = []
        total_rtt = 0.0

        for index in range(len(path) - 1):
            direction = (path[index], path[index + 1])
            link = network[direction]
            links.append(direction)
            link_bitrates.append(link.bitrate_mbps)
            total_rtt += link.rtt_ms / 1000  # Convert ms to seconds.

        routes.append({
            "nodes": path,
            "links": links,
            "bitrate_mbps": min(link_bitrates),
            "rtt_seconds": total_rtt,
        })
    return routes


def make_link_constraints(routes, network):
    """Build one capacity constraint for each directed link."""
    matrix = []
    limits = []

    for direction, link in network.items():
        row = []
        for route in routes:
            if direction in route["links"]:
                row.append(1)
            else:
                row.append(0)
        matrix.append(row)
        limits.append(link.bitrate_mbps)

    # Example: [1, 0, 1] means rate[0] + rate[2] <= this link's capacity.
    return matrix, limits


# STEP 3: Try a completion time T using linear programming.
def maximum_data_by_time(routes, matrix, limits, finish_time):
    """Find the most Mbits deliverable by finish_time, and the route rates."""
    available_times = []
    objective = []
    bounds = []

    for route in routes:
        available = max(0.0, finish_time - route["rtt_seconds"])
        available_times.append(available)

        # linprog MINIMIZES. Negative values make it MAXIMIZE delivered data.
        # Rate (Mbit/s) * available time (s) = delivered data (Mbit).
        objective.append(-available)
        if available == 0:
            bounds.append((0, 0))  # This route cannot contribute yet.
        else:
            bounds.append((0, None))

    solution = linprog(
        objective,
        A_ub=matrix,
        b_ub=limits,
        bounds=bounds,
        method="highs",
        options={
            "primal_feasibility_tolerance": 1e-9,
            "dual_feasibility_tolerance": 1e-9,
        },
    )
    if not solution.success:
        raise RuntimeError(f"Allocation solver failed: {solution.message}")

    rates = []
    total_megabits = 0.0
    for index in range(len(routes)):
        rate = max(0.0, float(solution.x[index]))
        rates.append(rate)
        total_megabits += rate * available_times[index]

    return total_megabits, rates


def round_byte_amounts(amounts, file_size_bytes):
    """Round shares to whole bytes while keeping the total exactly correct."""
    total_available = sum(amounts)
    if total_available <= 0:
        raise RuntimeError("The solver did not allocate any data")

    byte_counts = []
    remainders = []
    for index in range(len(amounts)):
        # Binary search may leave a little extra capacity. Scale it down.
        exact_bytes = amounts[index] / total_available * file_size_bytes
        whole_bytes = math.floor(exact_bytes)
        byte_counts.append(whole_bytes)
        remainders.append((exact_bytes - whole_bytes, index))

    # Give leftover bytes to the routes with the largest fractional parts.
    remainders.sort(reverse=True)
    leftover = file_size_bytes - sum(byte_counts)
    for position in range(leftover):
        index = remainders[position][1]
        byte_counts[index] += 1

    return byte_counts


# STEP 4: Use binary search to find the shortest feasible time.
def find_completion_time(routes, matrix, limits, required_megabits, best_single_time):
    lower = 0.0
    # One route alone can meet this upper bound. The margin avoids roundoff.
    upper = best_single_time + TIME_TOLERANCE_SECONDS
    for step in range(80):
        if upper - lower <= TIME_TOLERANCE_SECONDS:
            break
        middle = (lower + upper) / 2
        delivered, rates = maximum_data_by_time(routes, matrix, limits, middle)
        if delivered >= required_megabits:
            upper = middle
        else:
            lower = middle

    delivered, rates = maximum_data_by_time(routes, matrix, limits, upper)
    if delivered < required_megabits:
        raise RuntimeError("The final allocation cannot deliver the whole file")
    return upper, rates


# STEP 5: Convert rates into data amounts x_i and file shares w_i.
def calculate_file_shares(routes, rates, finish_time, file_size_bytes):
    amounts = []
    for index in range(len(routes)):
        available = max(0.0, finish_time - routes[index]["rtt_seconds"])
        # x_i = q_i * max(0, T - R_i) / 8.
        # Our q_i is in Mbit/s, so multiply by 1,000,000 to get bit/s.
        amounts.append(rates[index] * available * 1_000_000 / 8)
    byte_counts = round_byte_amounts(amounts, file_size_bytes)

    allocations = []
    completion_time = 0.0
    for index in range(len(routes)):
        if byte_counts[index] == 0:
            continue
        route = routes[index]
        # Rounding up a byte can slightly change the finishing time.
        route_time = route["rtt_seconds"] + byte_counts[index] * 8 / (rates[index] * 1_000_000)
        completion_time = max(completion_time, route_time)
        allocations.append({
            "nodes": route["nodes"],
            "links": route["links"],
            "rtt_seconds": route["rtt_seconds"],
            "rate_mbps": rates[index],
            "bytes": byte_counts[index],
            "weight": byte_counts[index] / file_size_bytes,  # w_i = x_i / S
            "finish_seconds": route_time,
        })
    return allocations, completion_time


def optimize_routes(routes, network, file_size_bytes):
    """Run steps 3-5 with ordinary function calls."""
    if type(file_size_bytes) is not int or file_size_bytes <= 0:
        raise ValueError("File size must be a positive whole number of bytes")
    if not routes:
        raise ValueError("At least one candidate route is required")

    required_megabits = file_size_bytes * 8 / 1_000_000
    matrix, limits = make_link_constraints(routes, network)

    # A single route gives a known feasible starting time for binary search.
    single_route_times = []
    for route in routes:
        time = route["rtt_seconds"] + required_megabits / route["bitrate_mbps"]
        single_route_times.append(time)
    best_single_time = min(single_route_times)
    best_single_index = single_route_times.index(best_single_time)

    # Step 4 calls step 3 repeatedly, trying shorter or longer deadlines.
    finish_time, rates = find_completion_time(
        routes, matrix, limits, required_megabits, best_single_time
    )

    # Step 5 turns the final rates into actual byte counts and weights.
    allocations, completion_time = calculate_file_shares(
        routes, rates, finish_time, file_size_bytes
    )

    return {
        "file_size_bytes": file_size_bytes,
        "candidate_count": len(routes),
        "best_single_path": routes[best_single_index]["nodes"],
        "best_single_seconds": best_single_time,
        "fluid_time_seconds": finish_time,
        "completion_time_seconds": completion_time,
        "allocations": allocations,
    }


def print_result(result):
    """Show only routes that receive at least one byte."""
    print(f"File size: {result['file_size_bytes']:,} bytes")
    print(f"Candidate routes: {result['candidate_count']}")
    single_path = " -> ".join(map(str, result["best_single_path"]))
    print(f"Best single route: {single_path} ({result['best_single_seconds']:.6f} s)")
    print(f"Parallel transfer estimate: {result['completion_time_seconds']:.6f} s")
    print()
    print(f"{'Route':<26} {'RTT (ms)':>10} {'Rate (Mbit/s)':>14} {'Bytes':>13} {'Share':>9}")
    for allocation in result["allocations"]:
        path = " -> ".join(map(str, allocation["nodes"]))
        print(
            f"{path:<26} {allocation['rtt_seconds'] * 1000:>10.3f} "
            f"{allocation['rate_mbps']:>14.3f} {allocation['bytes']:>13,} "
            f"{allocation['weight'] * 100:>8.3f}%"
        )
    print()
    print("Model estimate: constant link rates, streaming relays, and one RTT of startup delay.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=int, default=1, help="Sending node (default: 1)")
    parser.add_argument("--destination", type=int, default=4, help="Receiving node (default: 4)")
    parser.add_argument("--size-bytes", type=int, default=10_000_000, help="File size in bytes (default: 10000000)")
    args = parser.parse_args()

    try:
        network = build_network(NETWORK_LINKS)
        paths = find_all_paths(network, args.source, args.destination)  # Step 1
        routes = describe_routes(paths, network)                       # Step 2
        result = optimize_routes(routes, network, args.size_bytes)     # Steps 3-5
    except (ValueError, RuntimeError) as error:
        parser.exit(1, f"Error: {error}\n")

    print_result(result)


if __name__ == "__main__":
    main()
