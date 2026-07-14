from app.schemas.diagnosis import SubmissionDiagnosis


class SafetyService:
    def validate(
        self,
        diagnosis: SubmissionDiagnosis,
        hint_level: int,
    ) -> SubmissionDiagnosis:
        suspicious = ["完整代码如下", "可直接提交", "#include"]
        joined = "\n".join([diagnosis.analysis, *diagnosis.hints])
        if hint_level < 3 and any(token in joined for token in suspicious):
            raise ValueError("diagnosis may reveal a complete solution")
        return diagnosis
