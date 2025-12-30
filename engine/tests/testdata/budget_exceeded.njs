// njs module that exceeds the budget
// Uses Keys.* identifiers instead of raw numeric IDs
exports.meta = {
  name: "budget_exceeded",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML, Keys.SCORE_ADJUSTED, Keys.SCORE_FINAL],
  budget: {
    max_write_bytes: 1000000,  // High bytes limit
    max_write_cells: 10        // Very small - only 10 cells allowed
  }
};

exports.runBatch = function(objs, ctx, params) {
  // Allocating 100 cells should exceed the 10 cell limit
  var scores = ctx.batch.writeF32(Keys.SCORE_ML);
  return undefined;
};
