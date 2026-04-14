# Redis 依赖安装

Ubuntu 24.04 的 apt 仓库通常只有 `libhiredis-dev`，没有 `libredis++-dev`，因此建议这样安装：

```bash
sudo apt-get update
sudo apt-get install -y libhiredis-dev cmake g++ make pkg-config git

git clone https://github.com/sewenew/redis-plus-plus.git /tmp/redis-plus-plus
cmake -S /tmp/redis-plus-plus -B /tmp/redis-plus-plus/build \
  -DREDIS_PLUS_PLUS_BUILD_SHARED=ON \
  -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/redis-plus-plus/build -j
sudo cmake --install /tmp/redis-plus-plus/build

sudo ldconfig
```

安装完成后重新配置和构建：

```bash
cmake -S /home/max85/webserver/oj_platform -B /home/max85/webserver/oj_platform/build
cmake --build /home/max85/webserver/oj_platform/build -j
```

验证缓存是否生效：

```bash
curl -i http://127.0.0.1:18080/api/problems
curl -i http://127.0.0.1:18080/api/problems
```

第一次应看到 `X-Cache: MISS`，随后在 TTL 内再次访问应看到 `X-Cache: HIT`。