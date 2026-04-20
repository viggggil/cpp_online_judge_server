#include <bits/stdc++.h>
using namespace std;

using ll = long long;

static const int MOD1 = 1000000007;
static const int MOD2 = 1000000009;
static const int BASE1 = 911382323;
static const int BASE2 = 972663749;

struct Hash2 {
    int x, y;
    Hash2(int _x = 0, int _y = 0) : x(_x), y(_y) {}

    bool operator < (const Hash2& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
    bool operator == (const Hash2& other) const {
        return x == other.x && y == other.y;
    }
};

Hash2 addH(const Hash2& a, const Hash2& b) {
    int nx = a.x + b.x;
    int ny = a.y + b.y;
    if (nx >= MOD1) nx -= MOD1;
    if (ny >= MOD2) ny -= MOD2;
    return Hash2(nx, ny);
}

Hash2 subH(const Hash2& a, const Hash2& b) {
    int nx = a.x - b.x;
    int ny = a.y - b.y;
    if (nx < 0) nx += MOD1;
    if (ny < 0) ny += MOD2;
    return Hash2(nx, ny);
}

Hash2 mulH(const Hash2& a, const Hash2& b) {
    return Hash2(
        (int)(1LL * a.x * b.x % MOD1),
        (int)(1LL * a.y * b.y % MOD2)
    );
}

char flipBracket(char c) {
    if (c == '(') return ')';
    if (c == ')') return '(';
    if (c == '[') return ']';
    if (c == ']') return '[';
    if (c == '{') return '}';
    if (c == '}') return '{';
    if (c == '<') return '>';
    return '<';
}

bool isOpen(char c) {
    return c == '(' || c == '[' || c == '{' || c == '<';
}

bool isMatch(char l, char r) {
    if (l == '(') return r == ')';
    if (l == '[') return r == ']';
    if (l == '{') return r == '}';
    if (l == '<') return r == '>';
    return false;
}

// 只给左括号编码
int codeOf(char c) {
    if (c == '(') return 2;
    if (c == '[') return 3;
    if (c == '{') return 5;
    return 7; // '<'
}

struct Query {
    int l, r;
};

struct SolverOneSide {
    int n, m;
    string s;                 // 1-indexed: s[1..n]
    vector<Query> qs;         // 1-indexed queries in this orientation

    vector<int> badR;         // badR[l] = left endpoint l earliest invalid right endpoint
    vector<int> dsuNext;      // DSU / next pointer for assigning badR
    vector<vector<int>> qByR; // queries grouped by right endpoint, store query index

    const vector<Hash2>& pw;

    SolverOneSide(int _n, int _m, const string& _s, const vector<Query>& _qs, const vector<Hash2>& _pw)
        : n(_n), m(_m), s(_s), qs(_qs), pw(_pw) {
        badR.assign(n + 2, n + 1);
        dsuNext.resize(n + 2);
        qByR.assign(n + 1, {});
    }

    int findNext(int x) {
        if (x > n) return n + 1;
        if (dsuNext[x] == x) return x;
        return dsuNext[x] = findNext(dsuNext[x]);
    }

    // 给所有还没赋值的 badR[pos] (L <= pos <= R) 赋成 val
    void assignRange(int L, int R, int val) {
        if (L > R) return;
        int x = findNext(L);
        while (x <= R) {
            badR[x] = val;
            dsuNext[x] = findNext(x + 1);
            x = dsuNext[x];
        }
    }

    // 预处理 badR
    void buildBadR() {
        for (int i = 1; i <= n + 1; ++i) dsuNext[i] = i;

        vector<pair<int, char>> st; // (position, bracket char)

        for (int i = 1; i <= n; ++i) {
            char c = s[i];

            if (isOpen(c)) {
                st.push_back({i, c});
            } else {
                if (!st.empty() && isMatch(st.back().second, c)) {
                    int p = st.back().first;
                    st.pop_back();

                    // 左端点在 (p, i] 内会从 i 开始失效
                    assignRange(p + 1, i, i);
                } else {
                    // 一个坏右括号，所有 l <= i 都从 i 开始失效
                    assignRange(1, i, i);

                    // 官方题解这里要清空栈
                    st.clear();
                }
            }
        }
    }

    // 求当前栈序列 [fromIdx .. end] 的哈希
    // pre[k] = 前 k 个元素的哈希，pre[0]=空
    Hash2 getSuffixHash(const vector<Hash2>& pre, int fromIdx, int totalSize) {
        Hash2 left = pre[fromIdx];
        Hash2 right = pre[totalSize];
        return subH(right, mulH(left, pw[totalSize - fromIdx]));
    }

    // 统计这一侧所有“纯左括号型”的残留串哈希出现次数
    map<Hash2, int> collectLeftLike() {
        buildBadR();

        for (int i = 1; i <= m; ++i) {
            qByR[qs[i].r].push_back(i);
        }

        vector<pair<int, char>> st; // 当前真实括号栈，存 (position, char)
        vector<int> posStk;         // 当前栈里左括号的原下标，递增
        vector<Hash2> pre;          // 栈内容的前缀哈希
        pre.push_back(Hash2(0, 0));

        map<Hash2, int> cnt;

        for (int r = 1; r <= n; ++r) {
            char c = s[r];

            if (isOpen(c)) {
                st.push_back({r, c});
                posStk.push_back(r);

                Hash2 v(codeOf(c), codeOf(c));
                pre.push_back(addH(mulH(pre.back(), Hash2(BASE1, BASE2)), v));
            } else {
                if (!st.empty() && isMatch(st.back().second, c)) {
                    st.pop_back();
                    posStk.pop_back();
                    pre.pop_back();
                } else {
                    // 坏右括号，清空
                    st.clear();
                    posStk.clear();
                    pre.clear();
                    pre.push_back(Hash2(0, 0));
                }
            }

            // 回答所有右端点 = r 的询问
            for (int id : qByR[r]) {
                int l = qs[id].l;

                // 若 r >= badR[l]，说明 [l,r] 不可能是纯左括号型
                if (r >= badR[l]) continue;

                // 在当前栈里找第一个位置 >= l 的左括号
                int idx = lower_bound(posStk.begin(), posStk.end(), l) - posStk.begin();

                // 残留串就是这个后缀
                Hash2 h = getSuffixHash(pre, idx, (int)posStk.size());
                ++cnt[h];
            }
        }

        return cnt;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int T;
    cin >> T;

    const int LIM = 500000 + 5;
    vector<Hash2> pw(LIM);
    pw[0] = Hash2(1, 1);
    for (int i = 1; i < LIM; ++i) {
        pw[i] = mulH(pw[i - 1], Hash2(BASE1, BASE2));
    }

    while (T--) {
        int n, m;
        cin >> n >> m;

        string raw;
        cin >> raw;
        string s = " " + raw; // 1-indexed

        vector<Query> qs(m + 1);
        for (int i = 1; i <= m; ++i) {
            cin >> qs[i].l >> qs[i].r;
        }

        // 第一遍：统计原串中“纯左括号型”
        SolverOneSide A(n, m, s, qs, pw);
        map<Hash2, int> cntL = A.collectLeftLike();

        // 构造翻转后的串和询问
        string t = s;
        for (int i = 1; i <= n; ++i) {
            t[i] = flipBracket(t[i]);
        }
        reverse(t.begin() + 1, t.end());

        vector<Query> qs2(m + 1);
        for (int i = 1; i <= m; ++i) {
            qs2[i].l = n + 1 - qs[i].r;
            qs2[i].r = n + 1 - qs[i].l;
        }

        // 第二遍：统计原串中的“纯右括号型”
        SolverOneSide B(n, m, t, qs2, pw);
        map<Hash2, int> cntR = B.collectLeftLike();

        long long ans = 0;
        Hash2 EMPTY(0, 0);

        // 非空哈希：左残留和右残留配对
        for (auto &it : cntL) {
            if (it.first == EMPTY) continue;
            auto jt = cntR.find(it.first);
            if (jt != cntR.end()) {
                ans += min(it.second, jt->second);
            }
        }

        // 空串：只能彼此配对
        ans += cntL[EMPTY] / 2;

        cout << ans << '\n';
    }

    return 0;
}