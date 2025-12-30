// Valid njs module that writes to an allowed key
// Uses Keys.* identifiers instead of raw numeric IDs
exports.meta = {
  name: "valid_module",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  var n = ctx.batch.rowCount();
  var scores = ctx.batch.writeF32(Keys.SCORE_ML);
  for (var i = 0; i < n; i++) {
    scores[i] = 42.0;
  }
  return undefined;
};
