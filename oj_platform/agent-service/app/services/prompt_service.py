from app.schemas.diagnosis import DiagnosisRequest


class PromptService:
    def build_diagnosis_messages(
        self,
        request: DiagnosisRequest,
    ) -> list[dict[str, str]]:
        system_prompt = """
你是一名在线判题平台的 C++ 编程辅导老师。

规则：
1. 只根据提供的题目、用户代码、判题信息和对话历史进行分析。
2. 不编造隐藏测试数据。
3. 不提供完整可提交代码。
4. 使用中文解释。
5. 提示必须循序渐进，并遵守提示等级。
6. evidence 中只写能够从输入中直接确认的证据。
7. 只输出一个 JSON 对象，不要输出 Markdown。
""".strip()

        hint_policy = {
            1: "Level 1：只指出知识点或检查方向，不指出具体改法。",
            2: "Level 2：给出思考方向和局部排查建议，不给完整代码。",
            3: "Level 3：可以指出问题区域，可给伪代码或局部片段，禁止完整答案。",
        }[request.hint_level]

        history = "\n".join(
            f"第{item.round_no}轮 用户：{item.user_content}\n"
            f"第{item.round_no}轮 助手：{item.assistant_content}"
            for item in request.conversation.history[-8:]
        )

        problem = request.problem
        submission = request.submission
        user_prompt = f"""
提示等级：
{hint_policy}

题目：
ID: {problem.problem_id}
标题: {problem.title}
时间限制: {problem.time_limit_ms} ms
内存限制: {problem.memory_limit_mb} MB
标签: {", ".join(problem.tags)}
题面:
{problem.description_markdown}

用户提交：
submission_id: {submission.submission_id}
语言: {submission.language}
判题状态: {submission.judge_status}
耗时: {submission.execution_time_ms} ms
内存: {submission.memory_usage_kb} KB

源码：
```cpp
{submission.source_code}
```

编译输出：
```text
{submission.compiler_output}
```

运行错误：
```text
{submission.runtime_stderr}
```

最近对话历史：
{history or "无"}

用户问题：
{request.question or "请诊断这次提交。"}

请返回结构化诊断。
""".strip()

        return [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ]
