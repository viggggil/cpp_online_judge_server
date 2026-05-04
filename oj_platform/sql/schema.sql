CREATE DATABASE IF NOT EXISTS oj_platform
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE oj_platform;

CREATE TABLE IF NOT EXISTS users (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    role VARCHAR(16) NOT NULL DEFAULT 'user',
    created_at BIGINT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problems (
    id BIGINT NOT NULL PRIMARY KEY,
    title VARCHAR(255) NOT NULL,
    time_limit_ms INT NOT NULL DEFAULT 1000,
    memory_limit_mb INT NOT NULL DEFAULT 128,
    checker_type VARCHAR(64) NOT NULL DEFAULT 'default',
    created_at BIGINT NOT NULL DEFAULT 0,
    updated_at BIGINT NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problem_statements (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    language VARCHAR(16) NOT NULL,
    statement_markdown MEDIUMTEXT NOT NULL,
    UNIQUE KEY uk_problem_language (problem_id, language),
    CONSTRAINT fk_problem_statements_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problem_tags (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    tag VARCHAR(64) NOT NULL,
    UNIQUE KEY uk_problem_tag (problem_id, tag),
    CONSTRAINT fk_problem_tags_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problem_testcases (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    case_no INT NOT NULL,
    input_object_key VARCHAR(512) NOT NULL,
    output_object_key VARCHAR(512) NOT NULL,
    input_sha256 CHAR(64) NOT NULL,
    output_sha256 CHAR(64) NOT NULL,
    input_size_bytes BIGINT NOT NULL DEFAULT 0,
    output_size_bytes BIGINT NOT NULL DEFAULT 0,
    is_sample TINYINT(1) NOT NULL DEFAULT 0,
    UNIQUE KEY uk_problem_case_no (problem_id, case_no),
    CONSTRAINT fk_problem_testcases_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS submissions (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    submission_id VARCHAR(64) NOT NULL UNIQUE,
    user_id BIGINT NOT NULL,
    username_snapshot VARCHAR(32) NOT NULL,
    problem_id BIGINT NOT NULL,
    problem_id_text VARCHAR(32) NOT NULL,
    language VARCHAR(32) NOT NULL,
    source_code MEDIUMTEXT NOT NULL,
    status VARCHAR(64) NOT NULL,
    final_status VARCHAR(64) NOT NULL DEFAULT 'QUEUED',
    accepted TINYINT(1) NOT NULL DEFAULT 0,
    detail TEXT NOT NULL,
    compile_success TINYINT(1) NOT NULL DEFAULT 0,
    compile_stdout MEDIUMTEXT NOT NULL,
    compile_stderr MEDIUMTEXT NOT NULL,
    total_time_used_ms INT NOT NULL DEFAULT 0,
    peak_memory_used_kb INT NOT NULL DEFAULT 0,
    system_message MEDIUMTEXT NOT NULL,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL,
    KEY idx_submissions_user_created (user_id, created_at DESC),
    KEY idx_submissions_problem_created (problem_id, created_at DESC),
    CONSTRAINT fk_submissions_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_submissions_problem FOREIGN KEY (problem_id) REFERENCES problems(id) 
    ON DELETE CASCADE
    ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS submission_testcases (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    submission_db_id BIGINT NOT NULL,
    case_no INT NOT NULL,
    status VARCHAR(64) NOT NULL,
    time_used_ms INT NOT NULL DEFAULT 0,
    memory_used_kb INT NOT NULL DEFAULT 0,
    input_data MEDIUMTEXT NOT NULL,
    expected_output MEDIUMTEXT NOT NULL,
    actual_output MEDIUMTEXT NOT NULL,
    error_message MEDIUMTEXT NOT NULL,
    UNIQUE KEY uk_submission_case_no (submission_db_id, case_no),
    CONSTRAINT fk_submission_testcases_submission FOREIGN KEY (submission_db_id) REFERENCES submissions(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS assignments (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    title VARCHAR(255) NOT NULL,
    description_markdown MEDIUMTEXT NOT NULL,
    start_at BIGINT NOT NULL,
    end_at BIGINT NOT NULL,
    created_by BIGINT NOT NULL,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL,
    KEY idx_assignments_start_at (start_at DESC),
    KEY idx_assignments_created_by (created_by),
    CONSTRAINT fk_assignments_created_by
        FOREIGN KEY (created_by) REFERENCES users(id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS assignment_problems (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    assignment_id BIGINT NOT NULL,
    problem_id BIGINT NOT NULL,
    display_order INT NOT NULL,
    alias VARCHAR(32) NOT NULL,
    UNIQUE KEY uk_assignment_problem (assignment_id, problem_id),
    UNIQUE KEY uk_assignment_alias (assignment_id, alias),
    UNIQUE KEY uk_assignment_display_order (assignment_id, display_order),
    CONSTRAINT fk_assignment_problems_assignment
        FOREIGN KEY (assignment_id) REFERENCES assignments(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE,
    CONSTRAINT fk_assignment_problems_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
