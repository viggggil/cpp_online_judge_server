package main

import (
	"context"
	"database/sql"
	"net/http"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	mysqlDriver "github.com/go-sql-driver/mysql"
)

var mysqlDB *sql.DB

type MonitorSubmission struct {
	DBID              int64  `json:"db_id"`
	SubmissionID      string `json:"submission_id"`
	UserID            int64  `json:"user_id"`
	Username          string `json:"username"`
	ProblemID         int64  `json:"problem_id"`
	ProblemIDText     string `json:"problem_id_text"`
	Language          string `json:"language"`
	SourceCode        string `json:"source_code,omitempty"`
	SourceCodePreview string `json:"source_code_preview"`
	Status            string `json:"status"`
	FinalStatus       string `json:"final_status"`
	DisplayStatus     string `json:"display_status"`
	Accepted          bool   `json:"accepted"`
	Detail            string `json:"detail,omitempty"`
	CompileSuccess    bool   `json:"compile_success"`
	TimeUsedMS        int    `json:"time_used_ms"`
	MemoryUsedKB      int    `json:"memory_used_kb"`
	CreatedAt         int64  `json:"created_at"`
	UpdatedAt         int64  `json:"updated_at"`
}

type SubmissionsResponse struct {
	Limit       int                 `json:"limit"`
	TotalShown  int                 `json:"total_shown"`
	StatusCount map[string]int64    `json:"status_count"`
	Items       []MonitorSubmission `json:"items"`
}

func parseIntWithLimit(value string, defaultValue int, maxValue int) int {
	n, err := strconv.Atoi(value)
	if err != nil || n <= 0 {
		return defaultValue
	}
	if n > maxValue {
		return maxValue
	}
	return n
}

func initMySQLClient() error {
	cfg := mysqlDriver.NewConfig()

	cfg.User = getenv("OJ_MYSQL_USER", "oj")
	cfg.Passwd = getenv("OJ_MYSQL_PASSWORD", "oj123456")
	cfg.Net = "tcp"
	cfg.Addr = getenv("OJ_MYSQL_HOST", "mysql") + ":" + getenv("OJ_MYSQL_PORT", "3306")
	cfg.DBName = getenv("OJ_MYSQL_DATABASE", "oj_platform")

	cfg.ParseTime = true
	cfg.Loc = time.Local
	cfg.Params = map[string]string{
		"charset": "utf8mb4",
	}

	db, err := sql.Open("mysql", cfg.FormatDSN())
	if err != nil {
		return err
	}

	db.SetMaxOpenConns(parseIntWithLimit(getenv("OJ_MYSQL_MAX_OPEN_CONNS", "10"), 10, 100))
	db.SetMaxIdleConns(parseIntWithLimit(getenv("OJ_MYSQL_MAX_IDLE_CONNS", "5"), 5, 50))
	db.SetConnMaxLifetime(30 * time.Minute)

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		db.Close()
		return err
	}

	mysqlDB = db
	return nil
}

func normalizeSubmissionStatus(status string, finalStatus string) string {
	if status == "RUNNING" {
		return "running"
	}

	if finalStatus == "QUEUED" {
		return "queued"
	}

	if finalStatus != "" {
		return finalStatus
	}

	if status != "" {
		return status
	}

	return "unknown"
}

func sourcePreview(source string, maxRunes int) string {
	runes := []rune(source)
	if len(runes) <= maxRunes {
		return source
	}
	return string(runes[:maxRunes]) + "..."
}

func querySubmissionStatusCount(ctx context.Context) (map[string]int64, error) {
	rows, err := mysqlDB.QueryContext(ctx, `
		SELECT
			CASE
				WHEN status = 'RUNNING' THEN 'running'
				WHEN final_status = 'QUEUED' THEN 'queued'
				WHEN final_status <> '' THEN final_status
				WHEN status <> '' THEN status
				ELSE 'unknown'
			END AS display_status,
			COUNT(*) AS count
		FROM submissions
		GROUP BY display_status
	`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string]int64)

	for rows.Next() {
		var status string
		var count int64

		if err := rows.Scan(&status, &count); err != nil {
			return nil, err
		}

		result[status] = count
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return result, nil
}

func queryRecentSubmissions(ctx context.Context, limit int, includeCode bool) ([]MonitorSubmission, error) {
	rows, err := mysqlDB.QueryContext(ctx, `
		SELECT
			id,
			submission_id,
			user_id,
			username_snapshot,
			problem_id,
			problem_id_text,
			language,
			source_code,
			status,
			final_status,
			accepted,
			detail,
			compile_success,
			total_time_used_ms,
			peak_memory_used_kb,
			created_at,
			updated_at
		FROM submissions
		ORDER BY created_at DESC, id DESC
		LIMIT ?
	`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	items := make([]MonitorSubmission, 0, limit)

	for rows.Next() {
		var item MonitorSubmission
		var sourceCode string
		var acceptedInt int
		var compileSuccessInt int

		err := rows.Scan(
			&item.DBID,
			&item.SubmissionID,
			&item.UserID,
			&item.Username,
			&item.ProblemID,
			&item.ProblemIDText,
			&item.Language,
			&sourceCode,
			&item.Status,
			&item.FinalStatus,
			&acceptedInt,
			&item.Detail,
			&compileSuccessInt,
			&item.TimeUsedMS,
			&item.MemoryUsedKB,
			&item.CreatedAt,
			&item.UpdatedAt,
		)
		if err != nil {
			return nil, err
		}

		item.Accepted = acceptedInt != 0
		item.CompileSuccess = compileSuccessInt != 0
		item.DisplayStatus = normalizeSubmissionStatus(item.Status, item.FinalStatus)
		item.SourceCodePreview = sourcePreview(sourceCode, 200)

		if includeCode {
			item.SourceCode = sourceCode
		}

		items = append(items, item)
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return items, nil
}

func submissionsHandler(c *gin.Context) {
	if mysqlDB == nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "mysql client is not initialized",
		})
		return
	}

	limit := parseIntWithLimit(c.DefaultQuery("limit", "50"), 50, 200)
	includeCode := c.DefaultQuery("include_code", "0") == "1"

	ctx, cancel := context.WithTimeout(c.Request.Context(), 3*time.Second)
	defer cancel()

	statusCount, err := querySubmissionStatusCount(ctx)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error":  "failed to query submission status count",
			"detail": err.Error(),
		})
		return
	}

	items, err := queryRecentSubmissions(ctx, limit, includeCode)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error":  "failed to query recent submissions",
			"detail": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, SubmissionsResponse{
		Limit:       limit,
		TotalShown:  len(items),
		StatusCount: statusCount,
		Items:       items,
	})
}
