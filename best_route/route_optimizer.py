"""Find routes and decide how much of a file to send on each one."""

from scipy.optimize import linprog

# Change these values to try a different transfer.
SOURCE = 1
DESTINATION = 4
FILE_SIZE_BYTES = 10_000_000  # 10 MB


# This class only keeps a link's measurements together.
class Link:
    def __init__(self, start, end, bitrate_mbps, rtt_ms):
        self.start = start
        self.end = end
        self.bitrate_mbps = bitrate_mbps
        self.rtt_ms = rtt_ms


# Link(from_node, to_node, bitrate in Mbit/s, RTT in milliseconds)
# Each direction has its own bitrate. No CSV files are needed.
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


def find_paths(links, source, destination):
    paths = []
    waiting = [[source]]

    # A stack takes the last path added, so this is depth-first search.
    while waiting:
        path = waiting.pop()
        current = path[-1]

        if current == destination:
            paths.append(path)
        else:
            for link in links:
                if link.start == current and link.end not in path:
                    waiting.append(path + [link.end])

    return paths


def maximum_data_by_time(routes, link_rows, capacities, time):
    seconds_left = []
    costs = []
    bounds = []

    for route in routes:
        seconds = max(0, time - route["rtt_seconds"])
        seconds_left.append(seconds)
        # linprog minimizes, so a minus sign makes it maximize data.
        costs.append(-seconds)
        if seconds == 0:
            bounds.append((0, 0))
        else:
            bounds.append((0, None))

    # One row per link: the rates using that link cannot exceed its capacity.
    answer = linprog(
        costs,
        A_ub=link_rows,
        b_ub=capacities,
        bounds=bounds,
        method="highs",
        options={"primal_feasibility_tolerance": 1e-9,
                 "dual_feasibility_tolerance": 1e-9},
    )
    rates = []
    total_megabits = 0
    for i in range(len(routes)):
        rate = max(0, float(answer.x[i]))
        rates.append(rate)
        total_megabits += rate * seconds_left[i]
    return total_megabits, rates


def round_bytes(amounts, file_size):
    # Scale away any spare capacity, then round down to whole bytes.
    total = sum(amounts)
    byte_counts = []
    fractions = []
    for amount in amounts:
        exact = amount / total * file_size
        byte_counts.append(int(exact))
        fractions.append(exact - int(exact))

    # Give leftover bytes to the biggest fractional parts.
    leftover = file_size - sum(byte_counts)
    while leftover > 0:
        i = fractions.index(max(fractions))
        byte_counts[i] += 1
        fractions[i] = 0
        leftover -= 1
    return byte_counts


def calculate_plan(links, source, destination, file_size):
    # 1. Find every route that does not revisit a node.
    paths = find_paths(links, source, destination)

    # 2. Add up each route's RTT and record the links it uses.
    routes = []
    for path in paths:
        route_links = []
        rtt = 0
        bitrates = []
        for i in range(len(path) - 1):
            for link in links:
                if link.start == path[i] and link.end == path[i + 1]:
                    route_links.append((link.start, link.end))
                    rtt += link.rtt_ms / 1000  # milliseconds to seconds
                    bitrates.append(link.bitrate_mbps)
                    break
        routes.append({
            "nodes": path,
            "links": route_links,
            "rtt_seconds": rtt,
            "bitrate_mbps": min(bitrates),
        })

    # 3. Set up shared-link limits for maximum_data_by_time().
    link_rows = []
    capacities = []
    for link in links:
        row = []
        for route in routes:
            if (link.start, link.end) in route["links"]:
                row.append(1)
            else:
                row.append(0)
        link_rows.append(row)
        capacities.append(link.bitrate_mbps)
    # Example row [1, 0, 1]: rate[0] + rate[2] <= this link's bitrate.

    # 4. Find a short enough completion time using binary search.
    needed = file_size * 8 / 1_000_000  # bytes to megabits
    single_times = []
    for route in routes:
        single_times.append(route["rtt_seconds"] + needed / route["bitrate_mbps"])
    best_time = min(single_times)
    best_route = routes[single_times.index(best_time)]["nodes"]

    low = 0
    high = best_time + 0.000001  # One route can already meet this time.
    for step in range(80):
        if high - low <= 0.000001:
            break
        middle = (low + high) / 2
        delivered, rates = maximum_data_by_time(routes, link_rows, capacities, middle)
        if delivered >= needed:
            high = middle
        else:
            low = middle

    delivered, rates = maximum_data_by_time(routes, link_rows, capacities, high)

    # 5. Calculate x_i (bytes) and w_i (the fraction of the whole file).
    amounts = []
    for i in range(len(routes)):
        seconds = max(0, high - routes[i]["rtt_seconds"])
        amounts.append(rates[i] * 1_000_000 * seconds / 8)
    byte_counts = round_bytes(amounts, file_size)

    allocations = []
    finish_time = 0
    for i in range(len(routes)):
        if byte_counts[i] > 0:
            route = routes[i]
            # Recalculate the arrival time after rounding to whole bytes.
            arrival = route["rtt_seconds"] + byte_counts[i] * 8 / (rates[i] * 1_000_000)
            finish_time = max(finish_time, arrival)
            allocations.append({
                "nodes": route["nodes"],
                "links": route["links"],
                "rtt_seconds": route["rtt_seconds"],
                "rate_mbps": rates[i],
                "bytes": byte_counts[i],
                "weight": byte_counts[i] / file_size,
            })

    return {
        "candidate_count": len(paths),
        "best_single_path": best_route,
        "best_single_seconds": best_time,
        "completion_time_seconds": finish_time,
        "allocations": allocations,
    }


if __name__ == "__main__":
    result = calculate_plan(NETWORK_LINKS, SOURCE, DESTINATION, FILE_SIZE_BYTES)
    print("File size:", FILE_SIZE_BYTES, "bytes")
    print("Candidate routes:", result["candidate_count"])
    print("Best single route:", result["best_single_path"])
    print("Single route time:", round(result["best_single_seconds"], 6), "seconds")
    print("Parallel time:", round(result["completion_time_seconds"], 6), "seconds")

    for route in result["allocations"]:
        print()
        print("Route:", route["nodes"])
        print("RTT:", round(route["rtt_seconds"] * 1000, 3), "ms")
        print("Rate:", round(route["rate_mbps"], 3), "Mbit/s")
        print("Data:", route["bytes"], "bytes")
        print("Share:", round(route["weight"] * 100, 3), "%")

    print()
    print("Estimate only: constant rates, streaming relays, and one RTT of delay.")
