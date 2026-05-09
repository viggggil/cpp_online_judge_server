package main

import (
	"log"
	"net/http"
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

func workersHandler(cfg Config) gin.HandlerFunc {
	return func(c *gin.Context) {
		workers := cfg.WorkerHealthURLs

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
}

func main() {
	cfg := LoadConfig()

	initRedisClient()
	defer redisClient.Close()

	if err := initMySQLClient(); err != nil {
		log.Printf("mysql initialization deferred: %v", err)
	}
	if mysqlDB != nil {
		defer mysqlDB.Close()
	}

	r := gin.Default()

	api := r.Group("/api/monitor")
	{
		api.GET("/health", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{
				"service": "go_monitor",
				"status":  "ok",
			})
		})

		api.GET("/workers", workersHandler(cfg))
		api.GET("/summary", summaryHandler(cfg))
		api.GET("/queue", queueHandler)
		api.GET("/submissions", submissionsHandler)
	}

	r.Run(":" + cfg.Port)
}
