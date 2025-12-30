// Module that does NOT request IO capability
exports.meta = {
  name: "no_io_capability_test",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  // No capabilities.io declared
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  // Try to access ctx.io (should be undefined)
  if (ctx.io) {
    throw new Error("ctx.io should not be available");
  }

  // Just write dummy values
  var n = ctx.batch.rowCount();
  var output = ctx.batch.writeF32(Keys.SCORE_ML);
  for (var i = 0; i < n; i++) {
    output[i] = 1.0;
  }

  return undefined;
};
