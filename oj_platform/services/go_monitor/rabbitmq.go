package main

import (
	"context"
	"net/http"
	"time"
)

func checkRabbitMQHealth(ctx context.Context, cfg Config) BasicServiceStatus {
	start := time.Now()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, cfg.RabbitMQAPIURL, nil)
	if err != nil {
		return BasicServiceStatus{
			Name:      "rabbitmq",
			Alive:     false,
			Status:    "down",
			LatencyMS: 0,
			Error:     err.Error(),
		}
	}

	req.SetBasicAuth(cfg.RabbitMQUser, cfg.RabbitMQPassword)

	client := http.Client{
		Timeout: 2 * time.Second,
	}

	resp, err := client.Do(req)
	latency := time.Since(start).Milliseconds()

	if err != nil {
		return BasicServiceStatus{
			Name:      "rabbitmq",
			Alive:     false,
			Status:    "down",
			LatencyMS: latency,
			Error:     err.Error(),
		}
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return BasicServiceStatus{
			Name:      "rabbitmq",
			Alive:     false,
			Status:    "bad_status",
			LatencyMS: latency,
			Detail:    resp.Status,
		}
	}

	return BasicServiceStatus{
		Name:      "rabbitmq",
		Alive:     true,
		Status:    "ok",
		LatencyMS: latency,
		Detail:    resp.Status,
	}
}
