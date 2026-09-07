# EVERY FUNCTION A TRANSLATION UNIT DEFINES.
#
# A definition starts at column 0 - this tree indents everything nested - and its
# signature may wrap, so lines are glued into one logical construct before the
# test. Whichever of ';' and '{' comes FIRST decides: a ';' first is a
# declaration and is discarded, a '{' first is a definition and the identifier
# immediately before its argument list is emitted. Testing for ';' anywhere would
# throw away every one-line definition, whose body contains one.
{
  if (acc == "") {
    if ($0 !~ /^[A-Za-z_]/) next
    if ($0 ~ /^(struct|class|enum|union|namespace|typedef|template|extern|using|static_assert)[ \t{(]/) next
    acc = $0
  } else {
    acc = acc " " $0
  }
  sc = index(acc, ";")
  bc = index(acc, "{")
  if (sc > 0 && (bc == 0 || sc < bc)) { acc = ""; next }   # a declaration
  if (bc == 0) next                                        # keep gluing
  s = substr(acc, 1, bc - 1)
  if (s !~ /\(/) { acc = ""; next }                        # not a function
  sub(/\(.*$/, "", s)
  sub(/[ \t]+$/, "", s)
  if (match(s, /[A-Za-z_][A-Za-z0-9_]*$/)) print substr(s, RSTART, RLENGTH)
  acc = ""
}
