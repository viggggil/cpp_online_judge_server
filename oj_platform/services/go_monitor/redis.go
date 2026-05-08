package main

import (
	"context"
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/redis/go-redis/v9"
)

type RedisQueueItem struct {
	SubmissionID int64  `json:"submission_id,omitempty"`
	ProblemID    int64  `json:"problem_id,omitempty"`
	UserID       int64  `json:"user_id,omitempty"`
	Language     string `json:"language,omitempty"`
	Status       string `json:"status"`
	Raw          string `json:"raw,omitempty"`
}

type RedisQueueStatus struct {
	QueueName    string           `json:"queue_name"`
	PendingCount int64            `json:"pending_count"`
	Items        []RedisQueueItem `json:"items"`
}

var redisClient *redis.Client

func atoiDefault(value string, defaultValue int) int {
	if value == "" {
		return defaultValue
	}

	n, err := strconv.Atoi(value)
	if err != nil {
		return defaultValue
	}

	return n
}

func initRedisClient() {
	host := getenv("OJ_REDIS_HOST", "redis")
	port := getenv("OJ_REDIS_PORT", "6379")
	password := getenv("OJ_REDIS_PASSWORD", "")
	db := atoiDefault(getenv("OJ_REDIS_DB", "0"), 0)

	redisClient = redis.NewClient(&redis.Options{
		Addr:     host + ":" + port,
		Password: password,
		DB:       db,
	})
}

func getInt64FromMap(data map[string]interface{}, keys ...string) int64 {
	for _, key := range keys {
		value, ok := data[key]
		if !ok {
			continue
		}

		switch v := value.(type) {
		case float64:
			return int64(v)
		case int64:
			return v
		case int:
			return int64(v)
		case string:
			n, err := strconv.ParseInt(v, 10, 64)
			if err == nil {
				return n
			}
		}
	}

	return 0
}

func getStringFromMap(data map[string]interface{}, keys ...string) string {
	for _, key := range keys {
		value, ok := data[key]
		if !ok {
			continue
		}

		if str, ok := value.(string); ok {
			return str
		}
	}

	return ""
}

func parseRedisQueueItem(raw string) RedisQueueItem {
	item := RedisQueueItem{
		Status: "in_queue",
		Raw:    raw,
	}

	var data map[string]interface{}
	if err := json.Unmarshal([]byte(raw), &data); err != nil {
		return item
	}

	item.SubmissionID = getInt64FromMap(data, "submission_id", "submissionId", "id")
	item.ProblemID = getInt64FromMap(data, "problem_id", "problemId")
	item.UserID = getInt64FromMap(data, "user_id", "userId")
	item.Language = getStringFromMap(data, "language", "lang")

	return item
}

func getRedisQueueStatus(ctx context.Context) (RedisQueueStatus, error) {
	queueName := getenv("OJ_REDIS_QUEUE", "oj:queue:submissions")
	limit := atoiDefault(getenv("OJ_REDIS_QUEUE_SAMPLE_LIMIT", "50"), 50)

	pendingCount, err := redisClient.LLen(ctx, queueName).Result()
	if err != nil {
		return RedisQueueStatus{}, err
	}

	rawItems, err := redisClient.LRange(ctx, queueName, 0, int64(limit-1)).Result()
	if err != nil {
		return RedisQueueStatus{}, err
	}

	items := make([]RedisQueueItem, 0, len(rawItems))
	for _, raw := range rawItems {
		items = append(items, parseRedisQueueItem(raw))
	}

	return RedisQueueStatus{
		QueueName:    queueName,
		PendingCount: pendingCount,
		Items:        items,
	}, nil
}

func queueHandler(c *gin.Context) {
	ctx, cancel := context.WithTimeout(c.Request.Context(), 2*time.Second)
	defer cancel()

	status, err := getRedisQueueStatus(ctx)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "failed to read redis queue",
			"detail": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, status)
}
