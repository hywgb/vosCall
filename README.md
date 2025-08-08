# HyperSwitch (VOS-class SIP Switching System)

本项目提供等价覆盖并增强 VOS3000/VOS5000 的生产级语音交换系统实现蓝图：
- 控制面：OpenSIPS 3.x（B2BUA/路由/失败回退/并行分叉/拓扑隐藏）
- 媒体面：rtpengine 11.x（RTP 锚定、SRTP、RTCP 指标）
- 计费/路由/CDR/运维：C++20 微服务（route-svc、rating-billing、cdr-svc、auth-svc、admin-api、observe-svc、config-svc）
- 数据：PostgreSQL（账务/配置）、ClickHouse（CDR 明细）、Redis（缓存/限流）、Kafka（事件）、etcd（配置）

本仓库包含：
- 部署编排（docker-compose）：PostgreSQL、Redis、ClickHouse、Kafka/ZooKeeper、Prometheus、Grafana
- 数据库迁移 DDL（真实结构，禁止模拟数据）、真实汇率与 E.164 号段导入脚本
- OpenSIPS/rtpengine 完整配置模板（需根据生产环境 IP/证书调整）
- 微服务接口（proto/OpenAPI）与后续实现骨架

## 目录结构
```
/ deploy                # 编排与运维
  / opensips            # OpenSIPS 配置
  / rtpengine           # rtpengine 配置
  / prometheus          # Prometheus 配置
  docker-compose.yml
/ migrations
  / postgres            # PostgreSQL DDL & 约束
  / clickhouse          # ClickHouse DDL
/ scripts               # 数据拉取与装载（真实来源）
/ protos                # gRPC 接口定义
/ openapi               # 管理/客户 API 规范
```

## 快速启动（基础设施）
1) 准备本地/服务器 Docker 环境（Docker Engine ≥ 24, Compose ≥ v2）
2) 启动基础设施（PG/Redis/CH/Kafka/Prometheus/Grafana）：
```
cd deploy
docker compose up -d
```
3) 初始化数据库（必须）：
```
# PostgreSQL 结构
psql postgresql://admin:admin@127.0.0.1:5432/hyperswitch -f ../migrations/postgres/001_schema.sql
psql postgresql://admin:admin@127.0.0.1:5432/hyperswitch -f ../migrations/postgres/002_constraints.sql

# ClickHouse 结构
clickhouse-client --host 127.0.0.1 --query "CREATE DATABASE IF NOT EXISTS hyperswitch"
clickhouse-client --host 127.0.0.1 -d hyperswitch -mn < ../migrations/clickhouse/001_cdr_schema.sql
```
4) 导入真实数据（禁止模拟）：
- E.164 国家/地区/前缀：执行 `scripts/fetch_e164.sh` 获取权威数据源并转换为表加载文件，然后运行 `psql -f scripts/load_e164.sql`
- 汇率：执行 `scripts/fetch_ecb_fx.sh` 拉取 ECB 日汇率，随后 `psql -f scripts/load_fx.sql`
- 费率与目的地：从供应商 A-Z 费率（CSV/Excel）导入，使用 `scripts/rate_import.py`（稍后提供）完成校验与装载

## OpenSIPS/rtpengine 部署
- 调整 `deploy/opensips/opensips.cfg` 与 `deploy/rtpengine/rtpengine.conf` 中的本机 IP、证书与端口
- 在生产或 PoC 节点安装并启动 OpenSIPS/rtpengine，确保与本系统的 route-svc/cdr-svc URL 与 Redis/PG 可达

## 监控
- Prometheus: http://127.0.0.1:9090  (按需配置 Basic/Auth)
- Grafana: http://127.0.0.1:3000  (默认 admin/admin，首次登录请修改)

## 接口
- gRPC：见 `protos/`
- REST(OpenAPI)：见 `openapi/openapi.yaml`

## 安全与合规
- 启用 SIP-TLS 与 SRTP；限制管理端口来源；敏感数据脱敏；G.729 请确认法务许可与部署边界

## 下一步
- 按 `protos/` 实现 route-svc / rating-billing / cdr-svc 等微服务
- 集成 OpenSIPS 入口路由与失败回退/并行分叉
- 引入 RTCP 指标回灌质量路由；实现影子路由与灰度回滚

## 构建（C++20 微服务）
- 准备 vcpkg（可选）并设置环境变量：`export VCPKG_ROOT=...</path/to/vcpkg>`
- 配置与编译：
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行服务
- route-svc：`PG_URI=postgresql://admin:admin@127.0.0.1:5432/hyperswitch REDIS_URI=tcp://127.0.0.1:6379 BIND=0.0.0.0:7001 ./build/services/route-svc/route-svc`
- billing-svc：`PG_URI=postgresql://admin:admin@127.0.0.1:5432/hyperswitch BIND=0.0.0.0:7003 ./build/services/billing-svc/billing-svc`
- cdr-svc：`CH_HTTP=http://127.0.0.1:8123/?database=hyperswitch BIND=0.0.0.0:7002 ./build/services/cdr-svc/cdr-svc`

## OpenSIPS 对接（示意）
- 将 admin-api/gateway 实现的路由选择 HTTP 接口指向 `route-svc`（可通过轻量网关转 gRPC→HTTP），CDR 推送指向 `cdr-svc`

## admin-api（HTTP 网关）
- 构建后运行：
```
BIND=0.0.0.0:8080 ROUTE_SVC_ADDR=127.0.0.1:7001 ./build/apps/admin-api/admin-api
```
- OpenSIPS 中 `http_client_query` 指向 `http://127.0.0.1:8080/internal/route/pick`

## 数据装载（无 psql 客户端场景）
- 货币：`./build/tools/loader/loader currencies scripts/iso4217_currencies.csv`
- E.164：`./build/tools/loader/loader e164 scripts/e164_prefixes.csv`
- 汇率：建议使用 psql 与 `scripts/load_fx.sql`，或后续引入 loader 支持
