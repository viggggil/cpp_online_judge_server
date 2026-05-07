FROM ubuntu:24.04

WORKDIR /app

COPY go_monitor /app/go_monitor

RUN chmod +x /app/go_monitor

EXPOSE 18090

CMD ["/app/go_monitor"]
