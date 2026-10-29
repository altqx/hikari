# Korean (ko_KR) glossary

Terms and conventions for `i18n/hikarisub_ko_KR.ts`. Where the legacy catalog
(`i18n/hikarisub_gettext_ko_KR.ts`) already names a term, the interface keeps
that name so the two read alike. The English terms are those of `CONTEXT.md`.

## Style of the copy

- **Messages, questions and descriptions** use the formal 합니다/하십시오 style:
  "파일을 저장하지 못했습니다.", "변경 사항을 저장하시겠습니까?",
  "다시 시도하십시오." Tooltips and option descriptions that are sentences in
  English end the same way; those that are noun phrases stay noun phrases.
- **Menu items, buttons, labels, headings and column names** are short noun
  phrases or the verb's noun form, without a sentence ending: "열기", "저장",
  "다른 이름으로 저장…", "줄 삭제", "자동 저장 간격".
- **Status and progress** lines are noun phrases or 하는 중: "글꼴을 찾는 중…".
- **Placeholders** stay as in the source (`%1`, `%L1`, `%n`). A particle after a
  placeholder takes both forms, "%1(을)를", "%1(이)가", "%1(으)로", unless the
  sentence can be turned to need none ("파일: %1"). HikariSub and Aegisub end
  in a consonant (히카리섭, 에기섭): "HikariSub을", "HikariSub이", "HikariSub은".
- **Accelerators** follow the legacy catalog: the source's letter, uppercase,
  in brackets after the text: "자동화[&A]", "보기[&W]". `&&` is a literal `&`.
- **Units** keep their symbols (ms, cs, px); seconds and minutes are written
  in Korean ("%1초", "%1분 %2초"); a percentage has no space ("%1%").
- **"%1 of %2"** counters read "%1/%2".
- **Counts** (`numerus`): Korean has one form; it keeps `%n` and uses a counter
  where natural ("%n개", "%n줄", "%n초").
- The ellipsis character `…` and the sentence punctuation follow the source.
  Spacing follows standard Korean spacing (띄어쓰기), with a space before
  units only where Korean writes one.
- Product and format names stay in Latin script: HikariSub, Aegisub, ASS, SRT,
  Lua, MoonScript, DependencyControl, Hunspell, Harper, FFmpeg, Matroska/MKV,
  GitHub, VSFilter, libass.

## Terms

| English | Korean | Notes |
|---|---|---|
| Line | 줄 | A subtitle line; "selected lines" 선택한 줄 |
| Line group | 줄 그룹 | |
| Document | 문서 | The subtitle content |
| Document tab | 문서 탭 | |
| Untitled | 새 자막 | Legacy (the Untitled Document) |
| Session | 세션 | Legacy "Last session" stays 마지막 작업 |
| Workspace | 작업 공간 | |
| Panel | 패널 | |
| Grid (subtitle grid) | 자막 목록 | |
| Grid filter | 목록 필터 | |
| Editing target | 편집 대상 | |
| Tool target | 도구 대상 | |
| Protected reference | 보호된 참조 문서 | "Reference" alone 참조 |
| Line editor | 줄 편집기 | |
| Style | 스타일 | |
| Style manager | 스타일 관리 | Legacy |
| Style catalog | 스타일 목록 | Legacy "Catalog" 목록 |
| Font catalog | 글꼴 목록 | |
| Font | 글꼴 | |
| Font collector | 글꼴 수집기 | |
| Attachment | 첨부 파일 | |
| Script properties | 스크립트 속성 | |
| ASS override tag | ASS 재정의 태그 | "tag" alone 태그 |
| Visual tool | 시각 도구 | |
| Clip / vector clip | 클립 / 벡터 클립 | Legacy |
| Drawing / shape | 도형 | |
| Keyframe | 키 프레임 | Legacy |
| Frame | 프레임 | |
| Time shift | 시간 이동 | Legacy |
| Timing postprocessor | 타이밍 후처리기 | |
| Timing (as an activity) | 타이밍 | |
| Lead-in / lead-out | 도입부 / 종결부 | Legacy "lead-in" 도입부 |
| Start time / end time / duration | 시작 시간 / 종료 시간 / 길이 | |
| Karaoke | 노래방 | Legacy; "karaoke effect" 노래방 효과 |
| Syllable | 음절 | Legacy |
| Karaoke templater / template | 노래방 템플릿 실행기 / 템플릿 | |
| Typesetting | 식자 | |
| Furigana | 후리가나 | |
| Kanji timer | 한자 타이머 | |
| Translation mode | 번역 모드 | |
| Original text / Translated text | 원문 / 번역문 | |
| Unconfirmed | 미확인 | The review flag |
| Automation | 자동화 | Legacy |
| Automation macro / macro | 자동화 매크로 / 매크로 | |
| Script (automation) | 스크립트 | |
| Autoload | 자동 불러오기 | Legacy |
| Export filter | 내보내기 필터 | |
| User folder | 사용자 폴더 | |
| Settings / Options | 설정 | |
| Hotkey / shortcut | 단축키 | Legacy |
| Audio | 소리 | Legacy, kept for consistency with the finished copy |
| Volume | 음량 | |
| Spectrum / waveform | 스펙트럼 / 파형 | |
| Video | 영상 | Legacy |
| Subtitles | 자막 | |
| Actor | 배우 | Legacy |
| Effect (ASS field) | 효과 | Legacy |
| Layer | 레이어 | Legacy |
| Comment (line state) | 주석 | |
| Margin | 여백 | Legacy |
| Alignment | 정렬 | |
| Primary / secondary / outline / shadow colour | 기본 / 보조 / 외곽선 / 그림자 색상 | |
| Colour | 색상 | |
| Border / outline | 테두리 / 외곽선 | |
| Scale / rotation | 크기 / 회전 | |
| Resolution | 해상도 | |
| Spell check / spellchecker | 철자 검사 / 철자 검사기 | Legacy |
| Grammar check | 문법 검사 | |
| Dictionary | 사전 | |
| Find / replace | 찾기 / 바꾸기 | |
| Undo / redo | 실행 취소 / 다시 실행 | |
| Commit (a Line edit) | 적용 | |
| Autosave | 자동 저장 | Legacy |
| Recovery | 복구 | |
| Theme / accent colour | 테마 / 강조 색 | |
| Notice / toast | 알림 | |
| Log | 로그 | |
| Crash report / freeze report | 충돌 보고서 / 멈춤 보고서 | |
| Update | 업데이트 | |
| Preview | 미리 보기 | |
| Preset | 프리셋 | |
| Plugin | 플러그인 | |
| Legacy (HikariSub legacy, its installation) | 이전 버전 / 이전 버전 설치본 | "Import legacy settings" 이전 버전 설정 가져오기 |
| Pre-release | 시험판 | |
| Release notes / What's new | 릴리스 노트 / 새로운 기능 | |
| Home (page) | 홈 | |
| Recent files | 최근 파일 | |
| Layout | 레이아웃 | |
| Dock / undock / float | 도킹 / 도킹 해제 / 띄우기 | |
| Dismiss | 닫기 | |
| Reset / restore | 재설정 / 복원 | |
| Light / dark / high contrast | 밝게 / 어둡게 / 고대비 | Theme names |
| Inspector bar | 속성 막대 | |
| Active line | 활성 줄 | Legacy |
| Pick (lines for a tool) | 지정 | "Pick selected lines" 선택한 줄 지정 |
| Shape / drawing commands | 도형 / 그리기 명령 | |
| Bookmark | 책갈피 | |
| Folded / unfolded | 접힘 / 펼침 | |
| Uncommitted | 미적용 | |
| Characters per second (CPS) | 초당 문자 수 | |
| Frame rate (FPS) | 프레임 속도 | |
| Resample (subtitles) | 해상도 변경 | Legacy |
| Aspect ratio | 가로 세로 비율 | |
| Hue / saturation / value / lightness | 색조 / 채도 / 명도 / 밝기 | Value 명도 in the colour picker |
| Fallback font / font family | 대체 글꼴 / 글꼴 패밀리 | |
| Renderer | 렌더러 | |
| Output device | 출력 장치 | |
| Zoom | 확대/축소 | |
| Vocal onsets | 보컬 시작점 | |
| Romaji / kanji / reading | 로마자 / 한자 / 읽는 법 | |
| Strikeout | 취소선 | |
| Hard space | 고정 공백 | |
| Override block | 재정의 블록 | |
| Fix minor errors | 사소한 오류 수정 | |
| Suggestion | 제안 | |
| Reassign (a shortcut) | 재할당 | |
| Don't save / save and close | 저장 안 함 / 저장 후 닫기 | |
| Replace / replace all | 바꾸기 / 모두 바꾸기 | "Replace" an existing file or preset 교체 |
| Paste | 붙여 넣기 | |
| Duplicate (lines) | 복제 | |
| Proofreading | 교정 | Script properties |
| Getting started | 시작하기 | |
| Dialog (script dialog) | 대화 상자 | |
| Answers / last answers | 답변 / 마지막 답변 | Script dialog answers |
| Repeat last run | 마지막 실행 반복 | |
| Bundled (shipped with HikariSub) | 기본 제공 | |
| Template editor | 템플릿 편집기 | |
| Template line / code line / karaoke line | 템플릿 줄 / 코드 줄 / 노래방 줄 | "%n template lines" 템플릿 줄 %n개 |
| Template / code for each X | X별 템플릿 / X별 코드 | "Template for each syllable" 음절별 템플릿 |
| Pre-line template | 줄 앞 템플릿 | |
| Modifier (template) | 수정자 | Modifier names (noblank, loop) stay in Latin |
| Run (formatting run, Translation mode) | 서식 구간 | Short form 구간: "Run %1" 구간 %1 |
| Tap to time / tapping | 탭 타이밍 / 탭 | "Tap latency" 탭 지연 시간 |
| Beep (calibration) | 신호음 | |
| Shear (\fax, \fay) | 기울이기 | |
| Position pointer | 위치 포인터 | Visual tool names |
| Text positioning / moving / scaling | 텍스트 위치 지정 / 텍스트 이동 / 텍스트 크기 조정 | |
| Text Z rotation / X / Y rotation | 텍스트 Z 회전 / 텍스트 X / Y 회전 | |
| Rectangle / vector clipping | 사각형 클립 / 벡터 클립 | |
| Vector drawing | 도형 그리기 | Legacy |
| Position shifter | 위치 일괄 이동 | |
| Hydra / Perspective | 히드라 / 원근 | |
| Point / B-spline / Bézier curve | 점 / B-스플라인 / 베지어 곡선 | Drawing and clip tools; "Add line" there 직선 추가 |

## Hotkeys

- A command's name on the Hotkeys page reads as its menu item, without the
  accelerator: "Find and replace" 찾아서 바꾸기, "Font collector" 글꼴 수집기.
- A name ending in " alt" is the second key for the same command: the base
  name plus " (보조)", "Commit alt" 적용 (보조).
- Grid "Hide <column>" commands name the column: "Hide actor" 배우 열 숨김.
  In the Grid's Hide columns menu the item is the column name alone.
- Hotkey windows: Global 전역, Subtitles 자막 목록, Editor 편집기, Video 영상,
  Audio 소리.
