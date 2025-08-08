# 部署说明

- 使用 `docker-compose.yml` 启动 PostgreSQL / Redis / ClickHouse / Kafka / Prometheus / Grafana 基础设施
- OpenSIPS 与 rtpengine 请在专用主机或同机的 hostNetwork 下部署，按 `opensips.cfg` 与 `rtpengine.conf` 调整本机 IP、证书与端口
- 数据库初始化后，请执行 `scripts/fetch_ecb_fx.sh` 与 `scripts/fetch_e164.sh` 拉取真实数据，并通过 `load_*.sql` 装载

安全提示：生产环境请使用专用网络与 mTLS，限制管理面访问来源，并进行日志与审计。