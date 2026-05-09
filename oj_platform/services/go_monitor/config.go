package main

import "os"

type Config struct {
	Port string

	WorkerHealthURLs map[string]string

	MySQLHost     string
	MySQLPort     string
	MySQLUser     string
	MySQLPassword string
	MySQLDatabase string

	RedisAddr      string
	RedisPassword  string
	RedisDB        string
	RedisQueueName string
}

func getenv(key string, defaultValue string) string {
	value := os.Getenv(key)
	if value == "" {
		return defaultValue
	}
	return value
}

func LoadConfig() Config {
	return Config{
		Port: getenv("GO_MONITOR_PORT", "18090"),

		WorkerHealthURLs: map[string]string{
			"judge_worker_1": getenv("OJ_JUDGE_WORKER_HEALTH_1", "http://judge_worker_1:18081/api/health"),
			"judge_worker_2": getenv("OJ_JUDGE_WORKER_HEALTH_2", "http://judge_worker_2:18081/api/health"),
			"judge_worker_3": getenv("OJ_JUDGE_WORKER_HEALTH_3", "http://judge_worker_3:18081/api/health"),
		},

		MySQLHost:     getenv("OJ_MYSQL_HOST", "mysql"),
		MySQLPort:     getenv("OJ_MYSQL_PORT", "3306"),
		MySQLUser:     getenv("OJ_MYSQL_USER", "oj"),
		MySQLPassword: getenv("OJ_MYSQL_PASSWORD", "oj123456"),
		MySQLDatabase: getenv("OJ_MYSQL_DATABASE", "oj_platform"),

		RedisAddr:      getenv("OJ_REDIS_ADDR", "redis:6379"),
		RedisPassword:  getenv("OJ_REDIS_PASSWORD", ""),
		RedisDB:        getenv("OJ_REDIS_DB", "0"),
		RedisQueueName: getenv("OJ_REDIS_QUEUE", "oj:queue:submissions"),
	}
}
