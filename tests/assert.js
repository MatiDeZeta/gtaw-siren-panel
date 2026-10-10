'use strict';

let checks = 0, failures = 0;

function section(name) { console.log(`\n== ${name} ==`); }

function check(label, got, want) {
  checks++;
  if (String(got) !== String(want)) {
    failures++;
    console.log(`  FAIL ${label}`);
    console.log(`       got:      ${JSON.stringify(String(got))}`);
    console.log(`       expected: ${JSON.stringify(String(want))}`);
  } else {
    console.log(`  ok   ${label}  =  ${JSON.stringify(String(got))}`);
  }
}

function done() {
  console.log(`\n${checks - failures}/${checks} checks passed`);
  process.exit(failures ? 1 : 0);
}

module.exports = { section, check, done };
