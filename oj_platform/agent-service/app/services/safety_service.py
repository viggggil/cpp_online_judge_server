class SafetyService:
    def validate_answer(self, answer: str, hint_level: int) -> tuple[str, list[str]]:
        flags: list[str] = []
        lowered = answer.lower()
        if "int main(" in lowered or "int main (" in lowered:
            flags.append("contains_main_function")
        if lowered.count("```") >= 2 and len(answer) > 2500:
            flags.append("long_code_block")
        if hint_level <= 2 and flags:
            return (
                "我不能直接给出完整可提交代码。可以把你卡住的具体位置、"
                "题号或提交编号发给我，我会按提示等级给你拆成排查方向和局部思路。",
                flags,
            )
        return answer, flags
