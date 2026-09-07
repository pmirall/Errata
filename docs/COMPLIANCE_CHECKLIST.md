# Compliance checklist — what must be DONE before the first unit ships

The manual is the cheap half of compliance. This is the other half: the work
that happens outside the booklet, most of which the booklet cannot be honestly
printed without.

**Scope this assumes** (decided by the owner, 2026-09):

- Pebblebol is placed on the market as a **14+ electronic device, not a toy**.
  Directive 2009/48/EC, EN 71 and EN IEC 62115 are therefore out of scope.
- **Short maker run / crowdfunding**, sold in Spain and possibly elsewhere in
  the EU. Selling ten units is placing product on the market exactly as much as
  selling ten thousand; none of the obligations below scale down.

---

## Read this before anything else

**The radio is the expensive problem, and it is not solved by the manual.**

The ESP32-C3 SuperMini is a generic development board, not a radio module whose
RED certification you can inherit. Even with a genuinely pre-certified module,
a pre-certified module never exempts the finished product: your integration,
your antenna placement and your enclosure create an unintentional radiator of
their own, and it is the finished device that is placed on the market.

Testing the finished device — EN 300 328 (2.4 GHz), EN 301 489-1/-17 (EMC),
EN 62368-1 (electrical safety) — costs roughly **€3,000–8,000** against a bill
of materials of **€8.99**. That ratio, not the manual, is the decision that
governs whether this product ships.

**And nothing has been powered on yet.** The firmware has never run on physical
hardware (`CHANGELOG.md`, "Not verified"). You cannot sign a declaration of
conformity for a device that does not physically exist, and you cannot draw the
manual's exploded view until the enclosure is closed.

---

## The checklist

| # | Action | Self-declarable? | Rough cost | Blocks shipping |
|---|---|---|---|---|
| 1 | Register as an EEE producer in the **RII-AEE** (RD 110/2015) | Yes | Free | **Yes** — no number, no legal sale in Spain |
| 2 | Register in the **RII-PYA** (RD 710/2015) if cells are boxed with the device | Yes | Free | **Yes**, if cells ship |
| 3 | Contract a **SCRAP** (collective WEEE scheme: Ecolec, Ecotic, ...) | No, it is a contract | Membership + €/kg | **Yes** |
| 4 | Quarterly RII-AEE reporting of units placed on the market | Yes | Time | Ongoing |
| 5 | Assemble the **RED technical file** (design, risk analysis, test reports, DoC) | Yes, you write it | Time | **Yes** |
| 6 | **EN 300 328** radio test on the finished device | **No — test lab** | €2,000–4,000 | **Yes** |
| 7 | **EN 301 489-1/-17** EMC test | **No — test lab** | €1,000–2,500 | **Yes** |
| 8 | **EN 62368-1** electrical safety assessment | Often documentary below 3 V | €0–1,500 | **Yes** |
| 9 | **RED art. 3.3(d)** cybersecurity analysis / EN 18031 | Yes, with judgement | Time, or lab | Probably — see below |
| 10 | Collect **RoHS** supplier declarations for every part | Yes | Free | **Yes** |
| 11 | **REACH** SVHC check and art. 33 information duty | Yes | Free | **Yes** |
| 12 | Sign the **EU declaration of conformity**; publish it at a stable URL | Yes | Free | **Yes** — `contact.doc_url` |
| 13 | Apply the **CE mark** to the device (and the packaging where required) | Yes | Free | **Yes** |
| 14 | **GPSR** internal risk assessment; open and monitor the safety-report channel | Yes | Free | **Yes** — `contact.safety_email` |
| 15 | Publish a **coordinated vulnerability disclosure policy** and an **SBOM** | Yes | Time | Not yet — see CRA below |
| 16 | Product liability **insurance** | — | €200–600/yr | No, but do it anyway |
| 17 | Fill in every field of `docs/manual/product_facts.toml` | Yes | Time | **Yes** — the build refuses otherwise |

---

## The three that need a judgement call

**#9 — RED cybersecurity (Delegated Regulation (EU) 2022/30).** Mandatory since
1 August 2025 for internet-connected radio equipment. Pebblebol's position is
genuinely ambiguous and the file must record a reasoned answer rather than
assume one: the release firmware **never connects to the internet** — Wi-Fi is
scan-only and device-to-device traffic is ESP-NOW — but the device **does serve
an HTTP page over its own access point** for the creator. Decide it, write the
reasoning down, and if it lands in scope, EN 18031-1 applies.

**#15 — Cyber Resilience Act (EU) 2024/2847.** Reporting obligations applied
from 11 September 2026; the main obligations apply from **11 December 2027**.
This product will still be on sale then. Its Annex II is literally
"information and instructions to the user", and the manual already carries all
of it — single point of contact, disclosure policy, end-of-support date, SBOM
location, secure decommissioning — because putting it in now is free and
retrofitting it into a printed booklet is not. What remains is making those
URLs resolve to something real.

**Language.** Castilian is mandatory for sale in Spain (RDL 1/2007) and the
manual is bilingual ES/EN. Note that selling in **Catalonia** engages art.
128-1 of the Catalan Consumer Code, which entitles consumers to product
documentation and instructions **in Catalan**. Selling into other EU states
engages each state's own language rules. Neither is handled today; adding a
third language costs pages, so decide before the print run, not after.

---

## What has already been done

- The complete manual content, both languages, builds today:
  `tools/build_manual.sh --draft`.
- Every legally mandated field is present in the layout and wired to
  `product_facts.toml`, so filling them in is editing one file.
- The build **cannot** produce a print-ready PDF while any of them is missing.
