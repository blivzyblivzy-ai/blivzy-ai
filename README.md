# Blivzy AI v0.7 - Universal Chat

**Made by Blivzy.** One offline chat assistant for Flipper Zero (Momentum). Type naturally; Blivzy decides which built-in skill to use.

## What's new in v0.7

- **~63 new knowledge base entries (325 -> 388)**, added in two stress-test passes: wrote ~80 everyday questions, ran them through the actual fallback, kept only the ones that genuinely came back "I don't have a reliable answer," then wrote entries for those and re-ran to confirm. Two rounds brought unanswered questions on that test set from 26 down to 0.
- Round 1 - security/hacking concepts relevant to Flipper use (MITM, replay attacks, jamming, zero-days, SQL injection, XSS, 2FA, brute force, RSA, Caesar cipher, Tor, VPN, ransomware, social engineering, antivirus), plus physics/CS (quantum entanglement, hash tables, blockchain, Bitcoin, Schrodinger's cat, relativity, dark matter, age of the universe) and everyday science (yawning, deja vu, lock picking basics, Faraday cages, bullet/F1 speeds).
- Round 2 - networking (subnet mask, latency vs bandwidth, firewall, proxy, packet sniffer), automotive/RF security (car alarms, keyless entry relay, CAN bus, OBD-II, skimmers, wardriving), data structures (queue, stack, linked list, palindrome), and physics (torque, refraction, surface tension, impedance, why ice floats, how solar panels and microwaves work).
- Checked every candidate against the existing KB first so nothing duplicates an answer it could already give - this grows real coverage instead of padding the file. A few short lookups (`proxy`, `skimmer`, `queue`) got a bare one-word keyword added alongside the fuller phrase so short questions like "what is a proxy" match without needing every word of a longer keyword phrase present.

## What's new in v0.6

- **Chaining**: after any answer say `times 3`, `plus 5`, `/2` and it continues from the last result.
- **Two unknowns**: `solve x+y=10, x-y=2`, `2x+3y=12 and x-y=1`.
- **Calculus**: `derivative of x^2 at 3`, `integrate sin(x) from 0 to pi`, `sum of x^2 from 1 to 10` (Hungarian: `derivalt x^2 3-ban`, `integral x^2 0 3`).
- **Dates**: `days between 2026-01-01 and 2026-12-25`, `what day is 2000-01-01`, `2026-11-18 plus 100 days`, `days until 2026-12-25`.
- **Money and percent**: `loan 10000 5% 3 years`, `compound interest 1000 5% 10 years`, `what percent is 20 of 80`, `increase 80 by 15%` (Hungarian: `hitel`, `kamatos kamat`, `hany szazalek`).
- **Statistics**: variance, standard deviation and mode added to `average of ...`.
- **Smarter writing**: `write an essay about photosynthesis` / `irj esszet fotoszintezis` and paragraphs now fill the background from the knowledge base when it knows the topic.
- **Knowledge base**: ~320 English + ~260 Hungarian entries (physics, chemistry, earth science, maths formulas, computing, history, space, study and writing tips).

## What's new in v0.5

- **Hungarian keyboard**: in Hungarian mode the keyboard is QWERTZ. Press `#` to cycle letters -> symbols -> **accent letters** (a e i o u with acute, umlaut and double-acute marks drawn on the keys). The Flipper font has no accents, so accent letters are typed as plain letters and matching ignores accents. Pasted UTF-8 text (e.g. "Miért kék az ég?") is folded automatically.
- **Hungarian understanding**: the maths/convert/number tools now take Hungarian commands: `mennyi 15 szazaleka 80`, `harminc plusz negy`, `huszonot szor ketto`, `negyzetgyok 144`, `oldd meg 2x+3=11`, `5 km merfoldben`, `3,5 kg hany font`, `primszam-e 97`, `kor terulete sugar 5`. Decimal comma works and answers use a decimal comma.
- **Numbers as words** (English and Hungarian): `twenty five plus thirty`.
- **Hungarian writing helper**: essay, paragraph, email, speech, outline, pros and cons, thesis ideas and a short poem in Hungarian.
- **Much bigger knowledge base**: ~260 English + ~205 Hungarian entries (was 140 / 62): science, space, maths, Hungary (history, culture, inventors, food), tech, Flipper Zero, health, study/career tips, cooking, grammar.
- **Smarter search**: Hungarian suffix tolerance (`fotoszintezisrol`), automatic fallback to the other language's knowledge file, and a `more` / `tovabb` follow-up that opens the suggested related topic, plus `again` / `megint`.
- **Chat**: remembers your name (`my name is Sam` / `a nevem Anna`), Hungarian jokes, facts, quotes, stories, bye/sorry/thanks replies, more English jokes/facts/quotes.
- Bigger suggestion word lists (500+ English, 400+ Hungarian).

## What's new in v0.4

- **Symbol keyboard**: press `#` to switch to `+ - * / ^ ( ) = . , % ! x` and back. (v0.3 had no way to type operators!)
- **Smarter maths**: implicit multiplication (`2x`, `3(4+1)`), `sqrt 144`, words (`12 plus 30 divided by 6`, `15% of 80`, `square root of 144`), `gcd lcm ncr npr min max hypot root logb sinh cosh log2 fact rad deg`, constants `pi e tau phi ans`.
- **Equation solver for anything**: `solve 2x+3=11`, `x^2-5x+6=0`, `x^2=16` - numeric root finder, finds several roots in -200..200.
- **Unit converter**: length, mass, volume, time, temperature, speed, data. `5 km to miles`, `100 f to c`, `1 gb to mb`.
- **Number tools**: primes and factorisation, fibonacci, binary/hex/octal/roman, statistics (`average of 4 8 15 16`), gcd/lcm, BMI, tip, discount.
- **Geometry**: circle, sphere, cylinder, cube, rectangle, triangle, hypotenuse.
- **Writing helper**: essay, paragraph, email, speech, outline, pros and cons, thesis ideas, short poem, story starter (English; essay/paragraph/email also Hungarian-friendly).
- **Smart knowledge search**: 140 English + 60 Hungarian entries (v0.4; larger now) (was ~28 / ~28), typo tolerance (`photosintesis`, `compound intrest`), plural handling, "Related:" topic hints, and "try asking about" suggestions when it doesn't know.
- **Small talk & device info**: greetings, jokes, facts, quotes, time, date, battery, dice, coin, random numbers.
- **Chat flow**: Back from an answer returns to the keyboard so you can keep chatting; **History** of your last 8 questions; **What can I ask?** help; optional **chat log** to SD.
- Bigger prediction word lists, new preview screenshots, host test kit.

## Honest limits

The Flipper Zero has about 256 KB of RAM and no internet. This is **not** a ChatGPT-sized model. It is a fast rule-based assistant with a real maths engine and a searchable knowledge base. If something is not in its tools or KB files it says so. Writing templates have `[brackets]` for you to fill in - it does not invent facts.

## Install

1. Copy `app/blivzy_ai/` into `Momentum-Firmware/applications_user/blivzy_ai/`.
2. Build: `./fbt fap_blivzy_ai` (Windows: `fbt.cmd fap_blivzy_ai`).
3. Copy `blivzy_ai.fap` to `SD/apps/Tools/`.
4. Copy `sdcard/apps_data/blivzy_ai/` to `SD/apps_data/blivzy_ai/`.
5. Open **Apps -> Tools -> Blivzy AI**.

> v0.6 was verified by compiling the answer engine on a PC with mock headers (`tools/host_test/run.sh`). The Flipper GUI code has not been run on real hardware by the author of this edit - if your firmware's API names differ slightly, the compiler will point to the line.

## Teach it more

Edit `kb_en.txt` / `kb_hu.txt`, one entry per line: `keyword one,keyword two|Answer text.`
Longer, more specific keywords score higher. Multi-word keywords also match with small typos. Keep each line under about 450 characters. Hungarian files use ASCII (no accents) because the Flipper font has none.

## Controls

- D-pad moves on the keyboard, OK types, `>` sends, `<` deletes (hold to repeat), `#` cycles letters / symbols / (Hungarian) accents, `_` is space.
- Up from the top row opens suggestions.
- Back: answer -> keyboard -> menu -> exit.

## Try these

`25*48+7` - `times 3` - `solve x+y=10, x-y=2` - `derivative of x^2 at 3` - `loan 10000 5% 3 years` - `days between 2026-01-01 and 2026-12-25` - `mennyi 15 szazaleka 80` - `miert kek az eg` - `15% of 80` - `solve 2x+3=11` - `convert 5 km to miles` - `is 97 prime` - `average of 4 8 15 16` - `area of circle radius 5` - `write an essay about space` - `why is the sky blue` - `what time is it` - `tell me a joke`

**Blivzy AI - one chat, many tools.**
