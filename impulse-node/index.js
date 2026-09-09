const native = require('./build/Release/impulse_node_native.node');

class Traversal {
  constructor(snapshot, startNode) {
    this.snapshot = snapshot;
    this.startNode = BigInt(startNode);
    this.steps = [];
    this.params = {};
  }

  out(relation) {
    this.steps.push({ relation, direction: 'out' });
    return this;
  }

  in(relation) {
    this.steps.push({ relation, direction: 'in' });
    return this;
  }

  outFiltered(relation, filter) {
    this.steps.push({ relation, direction: 'out', filter });
    return this;
  }

  inFiltered(relation, filter) {
    this.steps.push({ relation, direction: 'in', filter });
    return this;
  }

  withParam(name, value) {
    const clean = name.replace(/^[$@]/, '');
    this.params[clean] = Number(value);
    return this;
  }

  toArray() {
    const ctx = new native.VmContext(this.snapshot);
    try {
      const qb = new native.QueryBuilder();
      let currentReg = 0;

      for (let i = 0; i < this.steps.length; i++) {
        const step = this.steps[i];
        const dstReg = (i % 2) + 1;
        const relId = typeof step.relation === 'number' ? step.relation : 0;

        if (i === 0) {
          qb.inputNode(0);
        }

        if (step.direction === 'out') {
          qb.walkEdge(relId, dstReg);
        } else {
          qb.walkCsc(relId, dstReg);
        }
        currentReg = dstReg;
      }

      qb.collectBitset(currentReg);
      const compiled = qb.compile();

      const state = new native.VmState();
      const queryRes = compiled.executeWithContext(ctx, state, this.startNode);

      const results = [];
      for (let i = 0; i < 65536; i++) {
        if (queryRes.testBitset(ctx, i)) {
          results.push(i);
        }
      }
      return results;
    } finally {
      ctx.destroy();
    }
  }

  toSet() {
    return new Set(this.toArray());
  }

  count() {
    return this.toArray().length;
  }
}

native.Snapshot.prototype.traverse = function (startNode) {
  return new Traversal(this, startNode);
};

module.exports = {
  ...native,
  Traversal,
};
