CREATE TABLE IF NOT EXISTS users (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problem_tags (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    tag VARCHAR(64) NOT NULL,
    UNIQUE KEY uk_problem_tag (problem_id, tag),
    CONSTRAINT fk_problem_tags_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS problem_testcases (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    case_no INT NOT NULL,
    input_data MEDIUMTEXT NOT NULL,
    expected_output MEDIUMTEXT NOT NULL,
    is_sample TINYINT(1) NOT NULL DEFAULT 0,
    UNIQUE KEY uk_problem_case_no (problem_id, case_no),
    CONSTRAINT fk_problem_testcases_problem
        FOREIGN KEY (problem_id) REFERENCES problems(id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;