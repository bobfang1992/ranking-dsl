// njs module that attempts to write to an unauthorized key
// Uses Keys.* identifiers instead of raw numeric IDs
exports.meta = {
  name: "unauthorized_write",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],  // Only score.ml is allowed
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  // This should FAIL - trying to write to SCORE_ADJUSTED which is NOT in meta.writes
  var scores = ctx.batch.writeF32(Keys.SCORE_ADJUSTED);
  return undefined;
};
