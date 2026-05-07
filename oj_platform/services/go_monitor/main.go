package main

import (
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

type WorkerStatus struct {
	Name   string `json:"name"`
	URL    string `json:"url"`
	Alive  bool   `json:"alive"`
	Status string `json:"status"`
	Error  string `json:"error,omitempty"`
}

func getenv(key string, defaultValue string) string {
	value := os.Getenv(key)
	if value == "" {
		return defaultValue
	}
	return value
}

func checkWorker(name string, url string) WorkerStatus {
	client := http.Client{
		Timeout: 2 * time.Second,
	}

	resp, err := client.Get(url)
	if err != nil {
		return WorkerStatus{
			Name:   name,
			URL:    url,
			Alive:  false,
			Status: "down",
			Error:  err.Error(),
		}
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return WorkerStatus{
			Name:   name,
			URL:    url,
			Alive:  false,
			Status: "bad_status",
		}
	}

	return WorkerStatus{
		Name:   name,
		URL:    url,
		Alive:  true,
		Status: "ok",
	}
}

func workersHandler(c *gin.Context) {
	workers := map[string]string{
		"judge_worker_1": getenv("OJ_JUDGE_WORKER_HEALTH_1", "http://judge_worker_1:18081/api/health"),
		"judge_worker_2": getenv("OJ_JUDGE_WORKER_HEALTH_2", "http://judge_worker_2:18081/api/health"),
		"judge_worker_3": getenv("OJ_JUDGE_WORKER_HEALTH_3", "http://judge_worker_3:18081/api/health"),
	}

	results := make([]WorkerStatus, 0, len(workers))
	resultChan := make(chan WorkerStatus, len(workers))

	var wg sync.WaitGroup

	for name, url := range workers {
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

	c.JSON(http.StatusOK, gin.H{
		"workers": results,
	})
}

func main() {
	r := gin.Default()

	api := r.Group("/api/monitor")
	{
		api.GET("/health", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{
				"service": "go_monitor",
				"status":  "ok",
			})
		})

		api.GET("/workers", workersHandler)
	}

	port := getenv("GO_MONITOR_PORT", "18090")
	r.Run(":" + port)
}
