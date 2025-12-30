// Valid njs module that writes to an allowed key
exports.meta = {
  name: "valid_module",
  version: "1.0.0",
  reads: [3001],  // score.base
  writes: [3002], // score.ml
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  var n = ctx.batch.rowCount();
  var scores = ctx.batch.writeF32(3002);
  for (var i = 0; i < n; i++) {
    scores[i] = 42.0;
  }
  return undefined;
};
