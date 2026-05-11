package main

import "testing"

func TestDecideOverallStatus(t *testing.T) {
	tests := []struct {
		name    string
		workers WorkersSummary
		redis   BasicServiceStatus
		mysql   BasicServiceStatus
		minio   BasicServiceStatus
		want    string
	}{
		{
			name: "all services ok",
			workers: WorkersSummary{
				Total: 3,
				Alive: 3,
			},
			redis: BasicServiceStatus{
				Alive: true,
			},
			mysql: BasicServiceStatus{
				Alive: true,
			},
			minio: BasicServiceStatus{
				Alive: true,
			},
			want: "ok",
		},
		{
			name: "redis down means overall down",
			workers: WorkersSummary{
				Total: 3,
				Alive: 3,
			},
			redis: BasicServiceStatus{
				Alive: false,
			},
			mysql: BasicServiceStatus{
				Alive: true,
			},
			minio: BasicServiceStatus{
				Alive: true,
			},
			want: "down",
		},
		{
			name: "mysql down means overall down",
			workers: WorkersSummary{
				Total: 3,
				Alive: 3,
			},
			redis: BasicServiceStatus{
				Alive: true,
			},
			mysql: BasicServiceStatus{
				Alive: false,
			},
			minio: BasicServiceStatus{
				Alive: true,
			},
			want: "down",
		},
		{
			name: "all workers down means overall down",
			workers: WorkersSummary{
				Total: 3,
				Alive: 0,
			},
			redis: BasicServiceStatus{
				Alive: true,
			},
			mysql: BasicServiceStatus{
				Alive: true,
			},
			minio: BasicServiceStatus{
				Alive: true,
			},
			want: "down",
		},
		{
			name: "some workers down means degraded",
			workers: WorkersSummary{
				Total: 3,
				Alive: 2,
			},
			redis: BasicServiceStatus{
				Alive: true,
			},
			mysql: BasicServiceStatus{
				Alive: true,
			},
			minio: BasicServiceStatus{
				Alive: true,
			},
			want: "degraded",
		},
		{
			name: "minio down means degraded",
			workers: WorkersSummary{
				Total: 3,
				Alive: 3,
			},
			redis: BasicServiceStatus{
				Alive: true,
			},
			mysql: BasicServiceStatus{
				Alive: true,
			},
			minio: BasicServiceStatus{
				Alive: false,
			},
			want: "degraded",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := decideOverallStatus(tt.workers, tt.redis, tt.mysql, tt.minio)

			if got != tt.want {
				t.Fatalf("decideOverallStatus() = %q, want %q", got, tt.want)
			}
		})
	}
}
