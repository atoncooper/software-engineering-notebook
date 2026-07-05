/**
 * @file graph.h
 * @topic 数据结构 - 图 (邻接矩阵/邻接表 + BFS/DFS + Dijkstra + Prim + Kruskal + 拓扑排序)
 *
 * @考点 408 大纲:数据结构 > 图
 *   - 存储:邻接矩阵 O(V²) / 邻接表 O(V+E)
 *   - BFS:队列+访问标记,求无权最短路径
 *   - DFS:栈/递归,求连通分量/环
 *   - Dijkstra:单源最短路径 (非负权)
 *   - Floyd:多源最短路径 O(V³),动态规划
 *   - Prim:点贪心 MST
 *   - Kruskal:边贪心 MST + 并查集
 *   - 拓扑排序:DAG,入度法 (Kahn) / DFS 后序逆序
 *
 * @业务 工业应用
 *   - 社交网络 (好友关系图)
 *   - 地图导航 (Dijkstra/A*)
 *   - 编译器依赖分析 (拓扑排序)
 *   - 网络拓扑 (OSPF 链路状态用 Dijkstra)
 *   - 推荐系统 (图神经网络)
 *
 * @陷阱 408 高频
 *   - Dijkstra 不能处理负权 (用 Bellman-Ford)
 *   - 邻接矩阵适合稠密图,邻接表适合稀疏图
 *   - BFS 求无权图最短路径;Dijkstra 求加权图最短路径
 *   - Kruskal 用并查集判环 O(E log E);Prim 用优先队列 O(E log V)
 *   - 拓扑排序仅适用 DAG,有环则无法排序
 */
#ifndef CS408_DS_GRAPH_GRAPH_H
#define CS408_DS_GRAPH_GRAPH_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <queue>
#include <stack>
#include <iostream>
#include <algorithm>
#include <climits>
#include <stdexcept>

namespace cs408::ds {

class Graph {
public:
    // 邻接表表示 (适合稀疏图)
    explicit Graph(int n) : adj_(n) {}

    void add_edge(int u, int v, int w = 1, bool directed = false) {
        adj_[u].push_back({u, v, w});
        if (!directed) adj_[v].push_back({v, u, w});
    }

    int size() const { return static_cast<int>(adj_.size()); }
    const std::vector<std::vector<Edge>>& adj() const { return adj_; }

    // ===== BFS =====
    std::vector<int> bfs(int src) const {
        std::vector<int> order;
        std::vector<bool> vis(adj_.size(), false);
        std::queue<int> q;
        q.push(src); vis[src] = true;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            order.push_back(u);
            for (const auto& e : adj_[u]) {
                if (!vis[e.to]) { vis[e.to] = true; q.push(e.to); }
            }
        }
        return order;
    }

    // ===== BFS 求无权最短路径 =====
    std::vector<int> bfs_shortest_path(int src, int dst) const {
        int n = static_cast<int>(adj_.size());
        std::vector<int> prev(n, -1);
        std::vector<bool> vis(n, false);
        std::queue<int> q; q.push(src); vis[src] = true;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            if (u == dst) break;
            for (const auto& e : adj_[u]) {
                if (!vis[e.to]) {
                    vis[e.to] = true; prev[e.to] = u; q.push(e.to);
                }
            }
        }
        std::vector<int> path;
        for (int cur = dst; cur != -1; cur = prev[cur]) path.push_back(cur);
        std::reverse(path.begin(), path.end());
        if (path.front() != src) return {};
        return path;
    }

    // ===== DFS (递归) =====
    std::vector<int> dfs(int src) const {
        std::vector<int> order;
        std::vector<bool> vis(adj_.size(), false);
        dfs_rec(src, vis, order);
        return order;
    }

    // ===== Dijkstra 单源最短路径 (非负权) =====
    std::vector<int> dijkstra(int src) const {
        int n = static_cast<int>(adj_.size());
        std::vector<int> dist(n, INT_MAX);
        std::vector<bool> done(n, false);
        dist[src] = 0;
        using P = std::pair<int,int>;  // (dist, vertex)
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0, src});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (done[u]) continue;
            done[u] = true;
            for (const auto& e : adj_[u]) {
                if (dist[u] + e.weight < dist[e.to]) {
                    dist[e.to] = dist[u] + e.weight;
                    pq.push({dist[e.to], e.to});
                }
            }
        }
        return dist;
    }

    // ===== Floyd 多源最短路径 O(V^3) =====
    std::vector<std::vector<int>> floyd() const {
        int n = static_cast<int>(adj_.size());
        std::vector<std::vector<int>> d(n, std::vector<int>(n, INT_MAX/2));
        for (int i = 0; i < n; ++i) d[i][i] = 0;
        for (int u = 0; u < n; ++u)
            for (const auto& e : adj_[u])
                d[u][e.to] = std::min(d[u][e.to], e.weight);
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    if (d[i][k] + d[k][j] < d[i][j])
                        d[i][j] = d[i][k] + d[k][j];
        return d;
    }

    // ===== Prim 最小生成树 (点贪心) =====
    int prim(int src = 0) const {
        int n = static_cast<int>(adj_.size());
        std::vector<int> key(n, INT_MAX);
        std::vector<bool> in_mst(n, false);
        key[src] = 0;
        int total = 0;
        using P = std::pair<int,int>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0, src});
        while (!pq.empty()) {
            auto [w, u] = pq.top(); pq.pop();
            if (in_mst[u]) continue;
            in_mst[u] = true;
            total += w;
            for (const auto& e : adj_[u]) {
                if (!in_mst[e.to] && e.weight < key[e.to]) {
                    key[e.to] = e.weight;
                    pq.push({e.weight, e.to});
                }
            }
        }
        return total;
    }

    // ===== Kruskal 最小生成树 (边贪心 + 并查集) =====
    int kruskal() const {
        std::vector<Edge> edges;
        for (const auto& list : adj_)
            for (const auto& e : list)
                if (e.from < e.to) edges.push_back(e);  // 无向图去重
        std::sort(edges.begin(), edges.end(),
                  [](const Edge& a, const Edge& b){ return a.weight < b.weight; });

        int n = static_cast<int>(adj_.size());
        std::vector<int> parent(n);
        for (int i = 0; i < n; ++i) parent[i] = i;
        auto find = [&](int x) {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
            return x;
        };

        int total = 0, cnt = 0;
        for (const auto& e : edges) {
            int ru = find(e.from), rv = find(e.to);
            if (ru != rv) {
                parent[ru] = rv;
                total += e.weight;
                if (++cnt == n - 1) break;
            }
        }
        return total;
    }

    // ===== 拓扑排序 (Kahn 入度法,仅 DAG) =====
    std::vector<int> topo_sort() const {
        int n = static_cast<int>(adj_.size());
        std::vector<int> indeg(n, 0);
        for (const auto& list : adj_)
            for (const auto& e : list) ++indeg[e.to];
        std::queue<int> q;
        for (int i = 0; i < n; ++i) if (indeg[i] == 0) q.push(i);
        std::vector<int> order;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            order.push_back(u);
            for (const auto& e : adj_[u]) {
                if (--indeg[e.to] == 0) q.push(e.to);
            }
        }
        return order;  // 若 size < n 说明有环
    }

private:
    std::vector<std::vector<Edge>> adj_;

    void dfs_rec(int u, std::vector<bool>& vis, std::vector<int>& order) const {
        vis[u] = true;
        order.push_back(u);
        for (const auto& e : adj_[u]) {
            if (!vis[e.to]) dfs_rec(e.to, vis, order);
        }
    }
};

// ===== 演示 =====
void graph_demo() {
    section("图 (无向)");
    Graph g(6);
    g.add_edge(0, 1, 4); g.add_edge(0, 2, 3);
    g.add_edge(1, 2, 1); g.add_edge(1, 3, 2);
    g.add_edge(2, 3, 4); g.add_edge(3, 4, 2);
    g.add_edge(4, 5, 6);

    std::cout << "BFS (从 0): "; print_vec(g.bfs(0));
    std::cout << "DFS (从 0): "; print_vec(g.dfs(0));
    std::cout << "BFS 最短路径 0->4: "; print_vec(g.bfs_shortest_path(0, 4));
    std::cout << "Dijkstra 单源最短 (0 出发): "; print_vec(g.dijkstra(0));
    std::cout << "Prim MST 总权: " << g.prim(0) << "\n";
    std::cout << "Kruskal MST 总权: " << g.kruskal() << "\n";

    section("拓扑排序 (DAG)");
    Graph dag(6);
    dag.add_edge(5, 2, 1, true);
    dag.add_edge(5, 0, 1, true);
    dag.add_edge(4, 0, 1, true);
    dag.add_edge(4, 1, 1, true);
    dag.add_edge(2, 3, 1, true);
    dag.add_edge(3, 1, 1, true);
    auto topo = dag.topo_sort();
    std::cout << "拓扑序: "; print_vec(topo);
}

bool graph_test() {
    Graph g(5);
    g.add_edge(0, 1, 4); g.add_edge(0, 2, 1);
    g.add_edge(1, 2, 2); g.add_edge(1, 3, 5);
    g.add_edge(2, 3, 2); g.add_edge(3, 4, 1);

    auto dist = g.dijkstra(0);
    CS408_EXPECT_EQ(dist[0], 0);
    CS408_EXPECT_EQ(dist[1], 3);   // 0->2->1
    CS408_EXPECT_EQ(dist[2], 1);
    CS408_EXPECT_EQ(dist[3], 3);   // 0->2->3
    CS408_EXPECT_EQ(dist[4], 4);

    auto path = g.bfs_shortest_path(0, 4);
    CS408_EXPECT_EQ(path.front(), 0);
    CS408_EXPECT_EQ(path.back(), 4);

    // 拓扑排序
    Graph dag(4);
    dag.add_edge(0, 1, 1, true);
    dag.add_edge(0, 2, 1, true);
    dag.add_edge(1, 3, 1, true);
    dag.add_edge(2, 3, 1, true);
    auto topo = dag.topo_sort();
    CS408_EXPECT_EQ(topo.size(), 4u);
    // 0 必须在 1/2/3 前
    auto idx = [&](int x){ return std::find(topo.begin(), topo.end(), x) - topo.begin(); };
    CS408_EXPECT(idx(0) < idx(1));
    CS408_EXPECT(idx(0) < idx(2));
    CS408_EXPECT(idx(1) < idx(3));
    CS408_EXPECT(idx(2) < idx(3));
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "graph", graph,
    "邻接矩阵/表;BFS 无权最短路;DFS;Dijkstra 非负权;Floyd O(V^3);Prim/Kruskal MST;拓扑 Kahn",
    "社交网络;地图导航 Dijkstra/A*;编译依赖拓扑;OSPF 链路状态;GNN 推荐",
    "Dijkstra 不处理负权 (用 Bellman-Ford);稠密图矩阵稀疏图表;Kruskal 并查集判环;DAG 才能拓扑排序",
    graph_demo, graph_test
);

} // namespace cs408::ds
#endif // CS408_DS_GRAPH_GRAPH_H
