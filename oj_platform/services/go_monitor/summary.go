package main

import (
	"context"
	"net/http"
	"sort"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

type BasicServiceStatus struct {
	Name      string `json:"name"`
	Alive     bool   `json:"alive"`
	Status    string `json:"status"`
	LatencyMS int64  `json:"latency_ms"`
	Detail    string `json:"detail,omitempty"`
	Error     string `json:"error,omitempty"`
}

type WorkersSummary struct {
	Status string         `json:"status"`
	Total  int            `json:"total"`
	Alive  int            `json:"alive"`
	Down   int            `json:"down"`
	Items  []WorkerStatus `json:"items"`
}

type MonitorSummary struct {
	Service   string             `json:"service"`
	Status    string             `json:"status"`
	CheckedAt int64              `json:"checked_at"`
	Workers   WorkersSummary     `json:"workers"`
	Redis     BasicServiceStatus `json:"redis"`
	MySQL     BasicServiceStatus `json:"mysql"`
	RabbitMQ  BasicServiceStatus `json:"rabbitmq"`
	MinIO     BasicServiceStatus `json:"minio"`
}

var mysqlInitMu sync.Mutex

func collectWorkerStatuses(cfg Config) []WorkerStatus {
	workers := cfg.WorkerHealthURLs
	results := make([]WorkerStatus, 0, len(workers))
	resultChan := make(chan WorkerStatus, len(workers))
	workerNames := make([]string, 0, len(workers))

	var wg sync.WaitGroup

	for name := range workers {
		workerNames = append(workerNames, name)
	}
	sort.Strings(workerNames)

	for _, name := range workerNames {
		url := workers[name]
		wg.Add(1)

		go func(name string, url string) {
			defer wg.Done()
			resultChan <- checkWorker(name, url)
		}(name, url)
	}

	wg.Wait()
	close(resultChan)

	for result := range resultChan {
		results = append(results, result)
	}

	sort.Slice(results, func(i int, j int) bool {
		return results[i].Name < results[j].Name
	})

	return results
}

func buildWorkersSummary(cfg Config) WorkersSummary {
	items := collectWorkerStatuses(cfg)
	aliveCount := 0
	for _, item := range items {
		if item.Alive {
			aliveCount++
		}
	}
	status := "ok"
	if aliveCount == 0 {
		status = "down"
	} else if aliveCount < len(items) {
		status = "degraded"
	}

	return WorkersSummary{
		Status: status,
		Total:  len(items),
		Alive:  aliveCount,
		Down:   len(items) - aliveCount,
		Items:  items,
	}
}

func checkRedisHealth(ctx context.Context) BasicServiceStatus {
	start := time.Now()

	if redisClient == nil {
		initRedisClient()
	}

	pong, err := redisClient.Ping(ctx).Result()
	latency := time.Since(start).Milliseconds()

	if err != nil {
		return BasicServiceStatus{
			Name:      "redis",
			Alive:     false,
			Status:    "down",
			LatencyMS: latency,
			Error:     err.Error(),
		}
	}

	return BasicServiceStatus{
		Name:      "redis",
		Alive:     true,
		Status:    "ok",
		LatencyMS: latency,
		Detail:    pong,
	}
}

func ensureMySQLClient() error {
	if mysqlDB != nil {
		return nil
	}

	mysqlInitMu.Lock()
	defer mysqlInitMu.Unlock()

	if mysqlDB != nil {
		return nil
	}

	return initMySQLClient()
}

func checkMySQLHealth(ctx context.Context) BasicServiceStatus {
	start := time.Now()

	if err := ensureMySQLClient(); err != nil {
		return BasicServiceStatus{
			Name:      "mysql",
			Alive:     false,
			Status:    "down",
			LatencyMS: time.Since(start).Milliseconds(),
			Error:     err.Error(),
		}
	}

	err := mysqlDB.PingContext(ctx)
	latency := time.Since(start).Milliseconds()

	if err != nil {
		return BasicServiceStatus{
			Name:      "mysql",
			Alive:     false,
			Status:    "down",
			LatencyMS: latency,
			Error:     err.Error(),
		}
	}

	return BasicServiceStatus{
		Name:      "mysql",
		Alive:     true,
		Status:    "ok",
		LatencyMS: latency,
	}
}

func checkHTTPHealth(ctx context.Context, name string, url string) BasicServiceStatus {
	start := time.Now()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return BasicServiceStatus{
			Name:      name,
			Alive:     false,
			Status:    "down",
			LatencyMS: 0,
			Error:     err.Error(),
		}
	}

	client := http.Client{
		Timeout: 2 * time.Second,
	}

	resp, err := client.Do(req)
	latency := time.Since(start).Milliseconds()

	if err != nil {
		return BasicServiceStatus{
			Name:      name,
			Alive:     false,
			Status:    "down",
			LatencyMS: latency,
			Error:     err.Error(),
		}
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return BasicServiceStatus{
			Name:      name,
			Alive:     false,
			Status:    "bad_status",
			LatencyMS: latency,
			Detail:    resp.Status,
		}
	}

	return BasicServiceStatus{
		Name:      name,
		Alive:     true,
		Status:    "ok",
		LatencyMS: latency,
		Detail:    resp.Status,
	}
}

func decideOverallStatus(
	workers WorkersSummary,
	redis BasicServiceStatus,
	mysql BasicServiceStatus,
	rabbitmq BasicServiceStatus,
	minio BasicServiceStatus,
) string {
	if !redis.Alive || !mysql.Alive || !rabbitmq.Alive || workers.Alive == 0 {
		return "down"
	}

	if workers.Alive < workers.Total || !minio.Alive {
		return "degraded"
	}

	return "ok"
}

func summaryHandler(cfg Config) gin.HandlerFunc {
	return func(c *gin.Context) {
		ctx, cancel := context.WithTimeout(c.Request.Context(), 3*time.Second)
		defer cancel()
		var workers WorkersSummary
		var redisStatus BasicServiceStatus
		var mysqlStatus BasicServiceStatus
		var rabbitmqStatus BasicServiceStatus
		var minioStatus BasicServiceStatus
		var wg sync.WaitGroup
		wg.Add(5)
		go func() {
			defer wg.Done()
			workers = buildWorkersSummary(cfg)
		}()
		go func() {
			defer wg.Done()
			redisStatus = checkRedisHealth(ctx)
		}()
		go func() {
			defer wg.Done()
			mysqlStatus = checkMySQLHealth(ctx)
		}()
		go func() {
			defer wg.Done()
			rabbitmqStatus = checkRabbitMQHealth(ctx, cfg)
		}()

		go func() {
			defer wg.Done()
			minioStatus = checkHTTPHealth(ctx, "minio", cfg.MinIOHealthURL)
		}()

		wg.Wait()

		overallStatus := decideOverallStatus(
			workers,
			redisStatus,
			mysqlStatus,
			rabbitmqStatus,
			minioStatus,
		)

		c.JSON(http.StatusOK, MonitorSummary{
			Service:   "go_monitor",
			Status:    overallStatus,
			CheckedAt: time.Now().Unix(),
			Workers:   workers,
			Redis:     redisStatus,
			MySQL:     mysqlStatus,
			RabbitMQ:  rabbitmqStatus,
			MinIO:     minioStatus,
		})
	}
}
