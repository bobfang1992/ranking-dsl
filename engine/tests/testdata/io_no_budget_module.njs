// Module that requests IO capability but doesn't set IO budgets
exports.meta = {
  name: "io_no_budget_test",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  capabilities: {
    io: {
      csv_read: true
    }
  },
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
    // NOTE: max_io_read_bytes and max_io_read_rows NOT set (default 0)
  }
};

exports.runBatch = function(objs, ctx, params) {
  // This should fail because IO budget is 0 (no IO allowed)
  var csv = ctx.io.readCsv(params.csv_file);
  return undefined;
};
