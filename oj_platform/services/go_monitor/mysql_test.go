package main

import "testing"

func TestParseSubmissionLimit(t *testing.T) {
	tests := []struct {
		name         string
		input        string
		defaultValue int
		maxValue     int
		want         int
	}{
		{
			name:         "valid number",
			input:        "20",
			defaultValue: 10,
			maxValue:     100,
			want:         20,
		},
		{
			name:         "empty value uses default",
			input:        "",
			defaultValue: 10,
			maxValue:     100,
			want:         10,
		},
		{
			name:         "invalid value uses default",
			input:        "abc",
			defaultValue: 10,
			maxValue:     100,
			want:         10,
		},
		{
			name:         "zero uses default",
			input:        "0",
			defaultValue: 10,
			maxValue:     100,
			want:         10,
		},
		{
			name:         "negative uses default",
			input:        "-1",
			defaultValue: 10,
			maxValue:     100,
			want:         10,
		},
		{
			name:         "too large uses max",
			input:        "999",
			defaultValue: 10,
			maxValue:     100,
			want:         100,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := parseSubmissionLimit(tt.input, tt.defaultValue, tt.maxValue)

			if got != tt.want {
				t.Fatalf("parseSubmissionLimit(%q) = %d, want %d", tt.input, got, tt.want)
			}
		})
	}
}

func TestNormalizeSubmissionStatus(t *testing.T) {
	tests := []struct {
		name        string
		status      string
		finalStatus string
		want        string
	}{
		{
			name:        "running status has priority",
			status:      "RUNNING",
			finalStatus: "QUEUED",
			want:        "running",
		},
		{
			name:        "queued final status",
			status:      "",
			finalStatus: "QUEUED",
			want:        "queued",
		},
		{
			name:        "final status result",
			status:      "",
			finalStatus: "AC",
			want:        "AC",
		},
		{
			name:        "fallback to status",
			status:      "PENDING",
			finalStatus: "",
			want:        "PENDING",
		},
		{
			name:        "empty becomes unknown",
			status:      "",
			finalStatus: "",
			want:        "unknown",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := normalizeSubmissionStatus(tt.status, tt.finalStatus)
			if got != tt.want {
				t.Fatalf("normalizeSubmissionStatus(%q, %q) = %q, want %q",
					tt.status,
					tt.finalStatus,
					got,
					tt.want,
				)
			}
		})
	}
}

func TestSubmissionSourcePreview(t *testing.T) {
	tests := []struct {
		name     string
		source   string
		maxRunes int
		want     string
	}{
		{
			name:     "short source unchanged",
			source:   "abc",
			maxRunes: 10,
			want:     "abc",
		},
		{
			name:     "long source truncated",
			source:   "abcdefghijklmnopqrstuvwxyz",
			maxRunes: 5,
			want:     "abcde...",
		},
		{
			name:     "unicode source truncated by rune",
			source:   "你好世界测试",
			maxRunes: 2,
			want:     "你好...",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := submissionSourcePreview(tt.source, tt.maxRunes)

			if got != tt.want {
				t.Fatalf("submissionSourcePreview(%q, %d) = %q, want %q",
					tt.source,
					tt.maxRunes,
					got,
					tt.want,
				)
			}
		})
	}
}
