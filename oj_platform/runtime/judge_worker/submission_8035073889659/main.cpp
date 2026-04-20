#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cassert>
using namespace std;
#define int long long
#define ld long double
#define ull unsigned long long
#define ll long long
const int MOD = 998244353;
const int INF = (1LL << 60);


struct node {
	int val, left,right,prev;
};
void solve() {
	int n;
	cin >> n;
	vector<node>arr(n + 2);
	auto cmp = [](const pair<int, int>& a, const pair<int, int>& b) {
		if (a.first != b.first) {
			return a.first <b.first;  
		}
		return a.second >b.second;    
		};

	priority_queue<
		pair<int, int>,
		vector<pair<int, int>>,
		decltype(cmp)
	> st(cmp);
	arr[0].left = 0;
	arr[0].right = 1;
	arr[0].val = -1000;
	arr[n + 1].left = n;
	arr[n + 1].right = n + 1;
	for (int i = 1; i <= n; i++) {
		cin >> arr[i].val;
		st.push({ arr[i].val,i });
		arr[i].left = i - 1;
		arr[i].right=i+1;
		arr[i].prev = -1;
	}
	int cnt = 0;
	while (!st.empty()) {
		auto x = st.top();
		int ind = x.second;
		int val = x.first;
		if (ind==1||arr[arr[ind].left].val != val - 1) {
			cnt += ind * (n - ind + 1);
		}
		else {
			arr[ind].prev = arr[ind].left;
			arr[arr[ind].right].left = arr[ind].left;
			arr[arr[ind].left].right = arr[ind].right;
		}
		st.pop();
	}
	for (int i = 1; i <= n; i++) {
		if (arr[i].prev == -1)continue;
		else cnt += (i-arr[i].prev) * (n - i + 1);
	}
	cout << cnt << "\n";
}
signed main() {
	int t;
	cin >> t;
	while (t--) {
		solve();
	}
	return 0;
}