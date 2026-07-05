/**
 * @file bst.h
 * @topic 数据结构 - 树 - 二叉搜索树 + 4 种遍历
 *
 * @考点 408 大纲:数据结构 > 树与二叉树 > 二叉搜索树
 *   - 性质:左子 < 根 < 右子;中序遍历得升序序列
 *   - 查找/插入/删除:平均 O(log n),最坏 O(n) (退化为链)
 *   - 删除节点 3 情况:叶子/单孩子/双孩子 (用后继替换)
 *   - 4 种遍历:先序/中序/后序/层序
 *   - 由先序+中序 或 后序+中序 唯一确定二叉树
 *
 * @业务 工业应用
 *   - C++ std::map/set 底层是红黑树 (平衡 BST)
 *   - 数据库索引 (B+树是平衡多叉 BST 推广)
 *   - 文件系统目录树
 *   - 路由表 LPM (Longest Prefix Match) 用 Trie 树
 *
 * @陷阱 408 高频
 *   - BST 删除双孩子节点:用直接后继 (右子树最小值) 替换
 *   - 退化为链表时性能 O(n) → 需平衡 (AVL/红黑树)
 *   - 中序遍历 + 栈可 O(1) 额外空间迭代
 *   - 完全二叉树节点编号:i 的孩子 2i+1 / 2i+2 (0-based)
 */
#ifndef CS408_DS_TREE_BST_H
#define CS408_DS_TREE_BST_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <queue>
#include <iostream>
#include <functional>

namespace cs408::ds {

template <typename T>
struct BSTNode {
    T data;
    BSTNode* left;
    BSTNode* right;
    explicit BSTNode(const T& v) : data(v), left(nullptr), right(nullptr) {}
};

template <typename T>
class BST {
public:
    using Node = BSTNode<T>;

    BST() : root_(nullptr) {}
    ~BST() { destroy(root_); }

    void insert(const T& v) { root_ = insert(root_, v); }
    void remove(const T& v) { root_ = remove(root_, v); }
    const Node* find(const T& v) const { return find(root_, v); }

    // 4 种遍历
    std::vector<T> preorder()  const { std::vector<T> r; pre(root_, r);  return r; }
    std::vector<T> inorder()   const { std::vector<T> r; in(root_, r);   return r; }
    std::vector<T> postorder() const { std::vector<T> r; post(root_, r); return r; }
    std::vector<T> levelorder() const {
        std::vector<T> r;
        if (!root_) return r;
        std::queue<Node*> q; q.push(root_);
        while (!q.empty()) {
            Node* n = q.front(); q.pop();
            r.push_back(n->data);
            if (n->left)  q.push(n->left);
            if (n->right) q.push(n->right);
        }
        return r;
    }

    int height() const { return height(root_); }

private:
    Node* root_;

    Node* insert(Node* n, const T& v) {
        if (!n) return new Node(v);
        if (v < n->data)      n->left  = insert(n->left, v);
        else if (v > n->data) n->right = insert(n->right, v);
        return n;
    }

    Node* remove(Node* n, const T& v) {
        if (!n) return nullptr;
        if (v < n->data)      n->left  = remove(n->left, v);
        else if (v > n->data) n->right = remove(n->right, v);
        else {
            // 找到节点
            if (!n->left && !n->right) { delete n; return nullptr; }
            if (!n->left)  { Node* r = n->right; delete n; return r; }
            if (!n->right) { Node* l = n->left;  delete n; return l; }
            // 双孩子:用右子树最小值 (直接后继) 替换
            Node* succ = n->right;
            while (succ->left) succ = succ->left;
            n->data = succ->data;
            n->right = remove(n->right, succ->data);
        }
        return n;
    }

    const Node* find(const Node* n, const T& v) const {
        if (!n) return nullptr;
        if (v == n->data) return n;
        return v < n->data ? find(n->left, v) : find(n->right, v);
    }

    void pre (const Node* n, std::vector<T>& r) const { if(!n) return; r.push_back(n->data); pre(n->left, r);  pre(n->right, r); }
    void in  (const Node* n, std::vector<T>& r) const { if(!n) return; in(n->left, r);  r.push_back(n->data); in(n->right, r); }
    void post(const Node* n, std::vector<T>& r) const { if(!n) return; post(n->left, r); post(n->right, r); r.push_back(n->data); }

    int height(const Node* n) const {
        if (!n) return 0;
        return 1 + std::max(height(n->left), height(n->right));
    }

    void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }
};

// ===== 演示 =====
void bst_demo() {
    section("BST 插入 50,30,70,20,40,60,80");
    BST<int> t;
    for (int x : {50, 30, 70, 20, 40, 60, 80}) t.insert(x);

    std::cout << "先序遍历 (根左右): "; print_vec(t.preorder());
    std::cout << "中序遍历 (左根右,升序): "; print_vec(t.inorder());
    std::cout << "后序遍历 (左右根): "; print_vec(t.postorder());
    std::cout << "层序遍历 (BFS): "; print_vec(t.levelorder());
    std::cout << "树高: " << t.height() << "\n";

    section("BST 删除双孩子节点 50 (用后继 60 替换)");
    t.remove(50);
    std::cout << "删除后中序: "; print_vec(t.inorder());
}

bool bst_test() {
    BST<int> t;
    for (int x : {50, 30, 70, 20, 40, 60, 80}) t.insert(x);
    CS408_EXPECT_EQ(t.inorder(), (std::vector<int>{20,30,40,50,60,70,80}));
    CS408_EXPECT(t.find(40) != nullptr);
    CS408_EXPECT(t.find(99) == nullptr);
    t.remove(20);  // 叶子
    CS408_EXPECT_EQ(t.inorder(), (std::vector<int>{30,40,50,60,70,80}));
    t.remove(70);  // 单孩子
    CS408_EXPECT_EQ(t.inorder(), (std::vector<int>{30,40,50,60,80}));
    t.remove(50);  // 双孩子
    CS408_EXPECT_EQ(t.inorder(), (std::vector<int>{30,40,60,80}));
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "tree.bst", bst,
    "BST 性质左<根<右;中序升序;删除双孩子用后继替换;4 种遍历;先+中或后+中唯一确定",
    "std::map/set 红黑树;数据库 B+树索引;文件系统目录;路由 Trie LPM",
    "删除双孩子用直接后继 (右子最小);退化为链表 O(n) 需平衡;完全二叉树 i 的孩子 2i+1/2i+2",
    bst_demo, bst_test
);

} // namespace cs408::ds
#endif // CS408_DS_TREE_BST_H
