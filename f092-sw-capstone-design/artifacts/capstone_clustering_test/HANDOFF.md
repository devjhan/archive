# HANDOFF.md — Agentic CS System 클러스터링 파이프라인

> 이 문서는 다른 AI 어시스턴트가 이 프로젝트를 이어받을 때 필요한 모든 맥락을 담고 있다.
> 마지막 업데이트: 2026-03-30

---

## 1. 프로젝트 개요

### 누구의 프로젝트인가
- **사용자**: 장한 (4학년 소프트웨어 공학부, MacBook 사용)
- **팀**: 5STONE (오스톤), 4인 캡스톤 디자인 팀
- **프로젝트명**: Agentic CS System — 기존 상담 데이터로부터 고객 지원 정책/워크플로우를 자동 생성하는 시스템

### 이 실험이 하는 일
고객 상담 대화 데이터에서 **고객의 의도(intent)를 자동으로 발견하고 분류**하는 파이프라인을 구축하고 검증한다. fine-tuned 소형 LLM 대신 **Gemini API**로 대체하는 실용적 접근을 사용한다.

### 핵심 논문 3편
1. **IDAS** (De Raedt et al., 2023) — LLM으로 발화를 의도 요약으로 변환하여 임베딩 품질을 높이는 전처리
2. **Dial-In LLM** (Hong et al., 2025, arXiv:2412.09049v4) — LLM-in-the-Loop 반복 클러스터링. Coherence Evaluator + Intent Labeller + 확률적 병합. **이 논문이 클러스터링 단계의 주 참조**
3. **FCSLM** — BERT PM(Prediction Model)이 Top-K 후보 + confidence를 산출하고, 저신뢰 쿼리에 대해서만 LLM을 호출하는 효율적 분류. Discriminative Intent Description으로 유사 의도를 구별

---

## 2. 데이터

### 원본 데이터셋
- **출처**: AI Hub "민간 민원 상담 LLM 사전학습 및 Instruction Tuning 데이터"
- **규모**: 11,033건 상담 (Train 9,773 / Val 1,260)
- **3개 도메인**:
  - 액티벤처 (여행): 900건
  - 엘지유플러스 (통신): 3,600건
  - 하나카드 (금융): 6,533건
- **포맷**: JSON (zip으로 압축됨)

### JSON 필드 구조 (원천데이터)
```json
{
  "source_id": "40001",
  "source": "엘지유플러스",
  "consulting_category": "배송문의",
  "client_gender": "",
  "client_age": "",
  "consulting_turns": "49",
  "consulting_length": 592,
  "consulting_content": "상담사: ... \n고객: ..."
}
```

### 라벨링 데이터 (별도 zip)
- `_분류`: 9개 카테고리 + 5축(상담 주제/결과/사유/요건/내용) 분류. **ground truth 참조용으로 사용**
- `_요약`: 상담 전체 요약. Pre-Init 품질 비교 reference로 사용
- `_질의응답`: QA 쌍. 파이프라인에 직접 사용하지 않음

### 개인정보 마스킹
원본 데이터에 이미 마스킹 적용됨: `<NAME>`, `<DATE>`, `<CHARGE>`, `▲▲▲` 등. 추가 처리 불필요.

### 인코딩 주의사항
장한이 macOS를 사용하므로, Google Drive에 올라간 파일명이 NFD 인코딩될 수 있다. 모든 텍스트 처리에 `unicodedata.normalize('NFC', text)`를 적용해야 한다.

---

## 3. 인프라 및 제약

### 실행 환경
- **Google Colab** (로컬 장비 없음)
- GPU: Tesla T4 (Colab 무료) — 임베딩 생성 및 BERT 학습에 필요
- LLM: **Gemini 2.5 flash-lite** API (종량제, ₩50,000 한도)
  - ⚠ gemini-2.0-flash-lite는 2026-06-01 서비스 종료 예정
  - Input: $0.10/1M tokens, Output: $0.40/1M tokens
- API 키: Colab '보안 비밀'의 `GOOGLE_API_KEY`

### 코딩 제약 (반드시 준수)
1. 모든 파일 경로는 `PATH_CONFIG` 딕셔너리로 관리 (pathlib.Path 사용)
2. 모든 노트북은 마크다운-코드 블럭 쌍 유지, 함수 단위로 작성
3. 모든 노트북의 첫 블럭: Drive 마운트 + 작업 디렉토리 변경 + 라이브러리 설치
4. 중간 산출물이 존재하면 새로 계산하지 않고 로드 (체크포인트 재개)
5. 각 코드 블럭은 독립 실행 가능 (환경/하이퍼파라미터/함수 정의 블럭 의존성은 예외)
6. 고강도 작업은 배치 단위 체크포인트 + tqdm 사용

### 예산 현황
| 항목 | 비용 |
|------|------|
| Pre-Init | ₩5,200 |
| Clustering | ₩1,000 |
| Description | ₩190 |
| Loop (모든 실험 합산) | ₩2,815 |
| **총 사용** | **₩9,205** |
| **잔여** | **₩40,795** |

---

## 4. 디렉토리 구조

```
내 드라이브/5stone_experiment/1_clustering_test/
├── data/
│   ├── raw/                          # 원천 + 라벨링 zip 파일
│   │   └── 23.민간 민원 상담.../3.개방데이터/1.데이터/
│   │       ├── Training/01.원천데이터/  (TS_*.zip)
│   │       ├── Training/02.라벨링데이터/ (TL_*_{분류,요약,질의응답}.zip)
│   │       ├── Validation/01.원천데이터/ (VS_*.zip)
│   │       └── Validation/02.라벨링데이터/ (VL_*.zip)
│   └── processed/                    # 전처리 완료 데이터
│       ├── eda_summary.json          # EDA 통계
│       ├── pre_init_intents.parquet  # 의도 분리·요약 결과 (34,025건)
│       ├── initial_intentset.parquet # 클러스터 할당 결과 (907 클러스터)
│       ├── intentset_summary.parquet # 클러스터 요약 (907행)
│       ├── intent_descriptions.parquet # Discriminative Description
│       ├── loop_results.parquet      # 최종 분류 결과
│       └── loop_evaluation.json      # 성능 메트릭
├── checkpoints/
│   ├── eda_source_df.parquet
│   ├── eda_label_df.parquet
│   ├── pre_init/                     # Pre-Init 배치 체크포인트
│   ├── clustering/
│   │   ├── iter_00.json, iter_01.json # 반복 클러스터링 상태
│   │   ├── labels.json               # 의도 라벨링 결과
│   │   └── merged_clusters.json      # 병합 결과
│   ├── descriptions/                 # Description 배치 체크포인트
│   └── loop/
│       ├── bert_pm/                  # BERT 모델 가중치
│       ├── bert_predictions.parquet  # Top-5 예측 (원본)
│       ├── bert_predictions_k10.parquet
│       ├── bert_predictions_domain_k10.parquet
│       ├── llm_predictions_t{0.3,0.5,0.7}.parquet
│       ├── llm_predictions_k10_t0.3.parquet
│       └── llm_predictions_domain_k10_t0.3.parquet
├── embeddings/
│   ├── intent_embeddings.npy         # 34,025 의도 임베딩 (768dim)
│   └── label_embeddings.npy          # 907 라벨 임베딩 (768dim)
├── vector_db/                        # FAISS 인덱스 (메모리에서 구축)
├── scripts/
│   ├── 00_directory_setup.ipynb
│   └── 00_eda_source_data.ipynb
├── notebooks/
│   ├── 01_pre_init_idas.ipynb
│   ├── 02_clustering_dial_in.ipynb
│   ├── 02_fix_merge.ipynb
│   ├── 03_discriminative_descriptions.ipynb
│   ├── 03_fix_fallback.ipynb
│   ├── 04_loop_inference.ipynb
│   └── 04_fix_threshold.ipynb        # threshold + Top-K + 도메인 필터 실험 통합
├── dataset.zip
└── guideline.pdf                     # 데이터셋 가이드라인 (미확인)
```

---

## 5. 파이프라인 단계별 상세

### Stage 1: Pre-Initialization (01_pre_init_idas.ipynb)

**목적**: 원본 상담 대화를 의도 단위로 분리하고 노이즈 제거된 요약문으로 변환

**입력**: `consulting_content` (상담사-고객 대화 원문)
**출력**: `pre_init_intents.parquet` — 34,025 의도 레코드

**방법**:
- Gemini 2.5 flash-lite로 상담 대화를 분석
- 복수 의도 포함 상담은 개별 의도로 분리 (92%가 복수 의도)
- 각 의도를 "행위 — 구체적 대상/맥락" 형식으로 요약
- `response_mime_type='application/json'`으로 structured output 강제

**출력 스키마**:
```
source_id, source, consulting_category, split, intent_index,
intent_summary, relevant_utterances (JSON string), n_intents,
is_fallback, raw_response
```

**주요 수치**: 11,033 → 34,025 (3.08x), Fallback 11건 (0.03%), 비용 ₩5,200

---

### Stage 2: Initialization (02_clustering_dial_in.ipynb + 02_fix_merge.ipynb)

**목적**: 의도 요약문을 클러스터링하여 초기 IntentSet 구축

**방법** (Dial-In LLM Algorithm 1):
1. KR-SBERT로 34,025 intent_summary 임베딩 생성 (L2 정규화)
2. 후보 K=[50,100,200,500,800,1000]으로 K-Means 클러스터링
3. 각 클러스터에서 20문장 랜덤 샘플링 → Gemini Coherence Evaluator (Good/Bad)
4. Good/Bad 비율 최대화하는 K 선택
5. Good 클러스터 확정, Bad 클러스터의 문장은 다음 반복으로
6. 반복 종료 후: Gemini로 "행위-목적" 라벨링 → geodesic distance + vMF 확률적 병합

**⚠ 병합 파라미터 수정 이력**:
- 초기 θ=0.8, τ=0.7 → 1,030→99 과도 병합 (1개 클러스터에 94.8% 집중)
- 수정 θ=0.3, τ=0.9 → 1,030→907 적정 병합
- 원인: 한국어 짧은 라벨("행위-목적")이 임베딩 공간에서 서로 가까움

**최종 결과**: 907 클러스터, 33,341문장 할당 (98%), 684 미할당 (2%)

**크로스 도메인 문제**: 437개 클러스터(48%)가 여러 도메인에 걸쳐 있음. "문의-결제방법" 같은 라벨이 금융/통신/여행 모두에 존재.

---

### Stage 3: Discriminative Description (03_discriminative_descriptions.ipynb + 03_fix_fallback.ipynb)

**목적**: 각 의도에 대해 유사 의도와 구별되는 설명 생성

**방법**:
1. 907 라벨 임베딩으로 FAISS Inner Product 인덱스 구축
2. 의도별 Top-5 유사 의도 검색 (geodesic < 1.0 필터)
3. Gemini로 구조화 JSON 생성

**출력 스키마** (`intent_descriptions.parquet`):
```
cluster_id, intent, definition, differs_from (JSON string), n_similar, is_fallback
```

**differs_from 예시**:
```json
[
  {"intent": "요청-결제취소", "distinction": "결제 완료 전 취소하는 것과 구별"},
  {"intent": "문의-환불절차", "distinction": "환불 절차 문의가 아닌 실제 환불 요청"}
]
```

**결과**: 828/907 정상 (91.3%), 79 fallback (8.7%, Gemini JSON 생성 실패). Fallback 의도는 Loop에서 라벨만으로 분류 참여.

---

### Stage 4: 추론 Loop (04_loop_inference.ipynb + 04_fix_threshold.ipynb)

**목적**: BERT PM + LLM 결합 분류 시스템 구축 및 성능 측정

**BERT PM 학습**:
- 모델: `klue/roberta-base`
- 입력: `intent_summary + [SEP] + relevant_utterances[:3]` (avg 100자)
- 클래스: 907개 (label2id 매핑)
- Train: 29,604건 (원본 split='train'), Test: 3,737건 (split='val')
- 5 epochs, batch 32, lr 2e-5
- 체크포인트: `checkpoints/loop/bert_pm/`

**추론 Loop 구조**:
1. BERT PM이 Test 배치에 Top-K 후보 + softmax confidence 산출
2. confidence ≥ θ → 고신뢰: BERT Top-1으로 확정
3. confidence < θ → 저신뢰: LLM 분류
   - 프롬프트: 쿼리 + BERT Top-1 예측 + Top-K 후보 + 각 후보의 Discriminative Description
   - `differs_from` 중 Top-K 내 의도 간 구별 기준만 삽입 (토큰 절약)
4. BERT Top-1 = LLM 예측 → 확정 (bert_llm_agree)
5. BERT ≠ LLM → LLM 결과 채택 (llm_override, Comparative Reasoning)
6. LLM이 "null" 판정 → null intent로 분류

**도메인 필터링** (source 필드 활용):
- BERT 모델은 1개 유지, 추론 시 해당 도메인의 label_id만 마스킹
- 전체 907 logit → 도메인별 logit 추출 → softmax → Top-K
- confidence 향상 (mean 0.211→0.248), 고신뢰 비율 증가 (24.7%→30.4%)

---

## 6. 실험 결과 (Ablation Study)

### Confidence Threshold 비교 (K=5)
| θ | 고신뢰 | 고신뢰 Acc | LLM 호출 | Null | Strict Acc | Non-null Acc |
|---|--------|-----------|---------|------|-----------|-------------|
| 0.3 | 924 (24.7%) | 79.5% | 2,813 | 508 | **51.19%** | **59.24%** |
| 0.5 | 439 (11.7%) | 90.9% | 3,298 | 551 | 50.07% | 58.73% |
| 0.7 | 168 (4.5%) | 95.2% | 3,569 | 556 | 49.56% | 58.22% |

### Top-K 비교 (θ=0.3)
| 설정 | Top-K Recall | Strict Acc | Null | LLM 순이득 |
|------|-------------|-----------|------|-----------|
| K=5 | 76.6% | 51.19% | 508 | +244 |
| K=10 | 84.5% | 52.34% | 381 | +257 |

### 도메인 필터링 비교 (θ=0.3, K=10)
| 설정 | BERT Top-1 | Strict Acc | Null | 고신뢰 |
|------|-----------|-----------|------|--------|
| 필터 없음 | 48.3% | 52.34% | 381 | 924 |
| 도메인 필터 | 48.5% | **52.61%** | **363** | **1,136** |

### 누적 개선
| 단계 | Strict Acc | 누적 개선 |
|------|-----------|---------|
| BERT only baseline | 48.33% | — |
| +LLM 통합 (K=5, θ=0.7) | 49.56% | +1.23%p |
| +Threshold 최적화 (θ=0.3) | 51.19% | +2.86%p |
| +Top-K 확장 (K=10) | 52.34% | +4.01%p |
| +도메인 필터 | **52.61%** | **+4.28%p** |

---

## 7. 알려진 문제 및 한계점

### 해결된 문제
1. **병합 과도**: θ=0.8에서 1,030→99 과도 병합 → θ=0.3, τ=0.9로 수정하여 907 클러스터 도출
2. **parquet 타입 에러**: `llm_agrees_bert` 컬럼에 bool + str 혼합 → 명시적 타입 캐스팅으로 해결
3. **Description fallback**: 503 에러 + JSON 파싱 실패 154건 → 재시도로 79건으로 감소

### 미해결 한계점
1. **907 클래스 구조적 난이도**: 클래스당 평균 ~32건. 1~5문장 소형 클러스터 186개. BERT가 본질적으로 어려움
2. **크로스 도메인 클러스터 48%**: 병합 단계에서 도메인 간 동일 라벨이 합쳐져서 도메인 필터링 효과 제한
3. **Null 과다 판정**: 최적 설정에서도 363건(9.7%)이 null. 실제 null보다 LLM 확신 부족이 원인 추정
4. **Coherence Evaluator 품질 미검증**: 논문의 fine-tuned 모델(96.3%) 대비 Gemini few-shot의 정확도 측정 안 됨
5. **guideline.pdf 미확인**: 데이터셋의 `consulting_category`가 수동 라벨인지 자동 태깅인지 확인 필요

---

## 8. 향후 과제 (구현되지 않은 것)

### 파이프라인 내 미구현
1. **Step D (APE)**: 자동 프롬프트 엔지니어링 — LLM 분류 프롬프트를 자동 최적화. 비용 추정 ~₩345
2. **다회 Loop**: null intent 후보를 IntentSet에 추가하고 BERT PM을 재학습하는 반복
3. **IntentSet 계층화**: 907개 클러스터를 상위 카테고리로 그룹화하여 BERT 부담 경감

### 구조적 개선 방향
4. **도메인별 별도 클러스터링**: 크로스 도메인 문제 해결을 위해 도메인별로 분리 클러스터링
5. **Convex sampling**: 현재 random sampling → convex hull 꼭짓점 샘플링으로 Coherence 평가 개선
6. **Context-Aware Role Separation**: Dial-In LLM 논문의 3.4절. 현재는 Pre-Init에서 고객 의도만 추출하여 생략

---

## 9. 주요 하이퍼파라미터 (현재 최적값)

```python
CONFIG = {
    # Gemini API
    'gemini_model': 'gemini-2.5-flash-lite',
    'gemini_temperature': 0.1,  # Coherence/분류용. Pre-Init은 0.2

    # 임베딩
    'embed_model_name': 'snunlp/KR-SBERT-V40K-klueNLI-augSTS',  # 768dim

    # 클러스터링 (Dial-In LLM)
    'cluster_candidate_k': [50, 100, 200, 500, 800, 1000],
    'cluster_max_iter': 5,
    'cluster_epsilon': 0.05,
    'cluster_sample_n': 20,  # Coherence 평가용 샘플 수

    # 병합 (⚠ 한국어 짧은 라벨에 맞게 강화된 값)
    'merge_geodesic_theta': 0.3,  # 논문 기본값 0.8에서 대폭 축소
    'merge_prob_tau': 0.9,        # 논문 기본값 0.7에서 강화
    'merge_kappa': 50.0,

    # Description
    'desc_top_k': 5,
    'desc_geodesic_max': 1.0,

    # BERT PM
    'bert_model_name': 'klue/roberta-base',
    'bert_max_length': 128,
    'bert_epochs': 5,
    'bert_batch_size': 32,
    'bert_lr': 2e-5,

    # Loop 추론 (최적 설정)
    'confidence_threshold': 0.3,
    'top_k_candidates': 10,
    'null_intent_enabled': True,
    'domain_filter_enabled': True,
}
```

---

## 10. 핵심 산출물 파일 가이드

| 파일 | 형식 | 행 수 | 용도 |
|------|------|------|------|
| `pre_init_intents.parquet` | parquet | 34,025 | 의도 분리 결과. `intent_summary`가 클러스터링 입력 |
| `initial_intentset.parquet` | parquet | 34,025 | 클러스터 할당 결과. `cluster_id`, `intent_label` 추가 |
| `intentset_summary.parquet` | parquet | 907 | 클러스터별 요약. `intent_label`, `size`, `sample_sentences` |
| `intent_descriptions.parquet` | parquet | 907 | Discriminative Description. `definition`, `differs_from` (JSON) |
| `loop_results.parquet` | parquet | 3,737 | 최종 분류 결과. `final_label`, `decision_source`, `is_null` |
| `loop_evaluation.json` | JSON | — | 전체 성능 메트릭 (threshold별, Top-K별, 도메인 필터별) |
| `intent_embeddings.npy` | numpy | (34025, 768) | intent_summary 임베딩 |
| `label_embeddings.npy` | numpy | (907, 768) | 클러스터 라벨 임베딩 |

---

## 11. 사용자의 선호 및 작업 스타일

- **언어**: 한국어로 대화. 기술 용어는 영어 혼용 OK
- **코드 스타일**: 함수 단위, 체크포인트 필수, tqdm으로 진행률 표시
- **결정 방식**: 선택지를 제시하면 빠르게 결정함. 이유를 짧게 설명하면 충분
- **노트북 구조**: 마크다운(설명) - 코드(구현) 쌍 엄격 유지
- **PATH_CONFIG**: 모든 경로를 pathlib.Path 딕셔너리로 관리 (한글 경로 대응)
- **NFC 정규화**: macOS ↔ Linux 인코딩 차이 때문에 모든 텍스트에 `unicodedata.normalize('NFC')` 적용
- **Gemini API 호출**: `response_mime_type='application/json'` + retry + rate limit delay 패턴
- **결과 공유 방식**: 실행된 .ipynb 파일을 업로드하면 출력을 파싱하여 검토

---

## 12. 이어서 할 수 있는 작업

**예산 ₩40,795 남아있음. 다음 중 선택 가능:**

1. **Step D (APE)** — LLM 분류 프롬프트 자동 최적화. ~₩345, ~25분
2. **다회 Loop** — null intent를 IntentSet에 추가 + BERT 재학습. Loop당 ~₩170
3. **IntentSet 계층화** — 907 → 상위 그룹 → 2단계 분류. BERT 부담 경감
4. **도메인별 별도 BERT** — 3개 도메인에 각각 전문 BERT 학습
5. **전체 파이프라인 발표 자료** — Gamma 프레젠테이션 생성 가능
