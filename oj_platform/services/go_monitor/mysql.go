package main

import (
	"context"
	"database/sql"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	_ "github.com/go-sql-driver/mysql"
)

var mysqlDB *sql.DB

type MonitorSubmission struct {
	ID                int64  `json:"id"`
	SubmissionID      string `json:"submission_id"`
	UserID            int64  `json:"user_id"`
	Username          string `json:"username"`
	ProblemID         int64  `json:"problem_id"`
	ProblemIDText     string `json:"problem_id_text"`
	Language          string `json:"language"`
	Status            string `json:"status"`
	FinalStatus       string `json:"final_status"`
	DisplayStatus     string `json:"display_status"`
	Accepted          bool   `json:"accepted"`
	CompileSuccess    bool   `json:"compile_success"`
	TimeUsedMS        int64  `json:"time_used_ms"`
	MemoryUsedKB      int64  `json:"memory_used_kb"`
	Detail            string `json:"detail,omitempty"`
	SourceCodePreview string `json:"source_code_preview"`
	SourceCode        string `json:"source_code,omitempty"`
	CreatedAt         int64  `json:"created_at"`
	UpdatedAt         int64  `json:"updated_at"`
}

type SubmissionsResponse struct {
	Limit      int                 `json:"limit"`
	ProblemID  string              `json:"problem_id,omitempty"`
	TotalShown int                 `json:"total_shown"`
	Items      []MonitorSubmission `json:"items"`
}

func parseSubmissionLimit(value string, defaultValue int, maxValue int) int {
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
	host := getenv("OJ_MYSQL_HOST", "mysql")
	port := getenv("OJ_MYSQL_PORT", "3306")
	user := getenv("OJ_MYSQL_USER", "oj")
	password := getenv("OJ_MYSQL_PASSWORD", "oj123456")
	database := getenv("OJ_MYSQL_DATABASE", "oj_platform")
	dsn := fmt.Sprintf(
		"%s:%s@tcp(%s:%s)/%s?charset=utf8mb4&parseTime=false&loc=Local",
		user,
		password,
		host,
		port,
		database,
	)

	db, err := sql.Open("mysql", dsn)
	if err != nil {
		return err
	}

	db.SetMaxOpenConns(10)
	db.SetMaxIdleConns(5)
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
	status = strings.TrimSpace(status)
	finalStatus = strings.TrimSpace(finalStatus)
	if status == "RUNNING" {
		return "running"
	}
	if finalStatus != "" && finalStatus != "QUEUED" {
		return finalStatus
	}
	if finalStatus == "QUEUED" {
		return "queued"
	}
	if status != "" {
		return status
	}
	return "unknown"
}

func submissionSourcePreview(source string, maxRunes int) string {
	runes := []rune(source)
	if len(runes) <= maxRunes {
		return source
	}
	return string(runes[:maxRunes]) + "..."
}

func queryRecentSubmissions(ctx context.Context, limit int, problemIDFilter string, includeCode bool) ([]MonitorSubmission, error) {
	baseSQL := `
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
			compile_success,
			total_time_used_ms,
			peak_memory_used_kb,
			detail,
			created_at,
			updated_at
		FROM submissions
	`
	args := make([]any, 0)
	problemIDFilter = strings.TrimSpace(problemIDFilter)
	if problemIDFilter != "" {
		baseSQL += `
		WHERE (
			problem_id_text = ?
		`
		args = append(args, problemIDFilter)
		if problemIDNumber, err := strconv.ParseInt(problemIDFilter, 10, 64); err == nil {
			baseSQL += `
			OR problem_id = ?
			`
			args = append(args, problemIDNumber)
		}
		baseSQL += `
		)
		`
	}
	baseSQL += `
		ORDER BY created_at DESC, id DESC
		LIMIT ?
	`
	args = append(args, limit)
	rows, err := mysqlDB.QueryContext(ctx, baseSQL, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	items := make([]MonitorSubmission, 0, limit)
	for rows.Next() {
		var item MonitorSubmission
		var username sql.NullString
		var problemIDText sql.NullString
		var language sql.NullString
		var sourceCode sql.NullString
		var status sql.NullString
		var finalStatus sql.NullString
		var detail sql.NullString
		var acceptedInt sql.NullInt64
		var compileSuccessInt sql.NullInt64
		err := rows.Scan(
			&item.ID,
			&item.SubmissionID,
			&item.UserID,
			&username,
			&item.ProblemID,
			&problemIDText,
			&language,
			&sourceCode,
			&status,
			&finalStatus,
			&acceptedInt,
			&compileSuccessInt,
			&item.TimeUsedMS,
			&item.MemoryUsedKB,
			&detail,
			&item.CreatedAt,
			&item.UpdatedAt,
		)
		if err != nil {
			return nil, err
		}
		item.Username = username.String
		item.ProblemIDText = problemIDText.String
		item.Language = language.String
		item.Status = status.String
		item.FinalStatus = finalStatus.String
		item.Detail = detail.String
		item.Accepted = acceptedInt.Valid && acceptedInt.Int64 != 0
		item.CompileSuccess = compileSuccessInt.Valid && compileSuccessInt.Int64 != 0
		item.DisplayStatus = normalizeSubmissionStatus(item.Status, item.FinalStatus)
		item.SourceCodePreview = submissionSourcePreview(sourceCode.String, 200)
		if includeCode {
			item.SourceCode = sourceCode.String
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
	limit := parseSubmissionLimit(c.DefaultQuery("limit", "20"), 20, 100)
	problemID := strings.TrimSpace(c.Query("problem_id"))
	includeCode := c.DefaultQuery("include_code", "0") == "1"
	ctx, cancel := context.WithTimeout(c.Request.Context(), 3*time.Second)
	defer cancel()
	items, err := queryRecentSubmissions(ctx, limit, problemID, includeCode)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error":  "failed to query recent submissions",
			"detail": err.Error(),
		})
		return
	}
	c.JSON(http.StatusOK, SubmissionsResponse{
		Limit:      limit,
		ProblemID:  problemID,
		TotalShown: len(items),
		Items:      items,
	})
}
