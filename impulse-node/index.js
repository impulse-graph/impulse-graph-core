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
      qb.inputNode(0);

      for (let i = 0; i < this.steps.length; i++) {
        const step = this.steps[i];
        const relId = typeof step.relation === 'number' ? step.relation : 0;

        if (step.direction === 'out') {
          if (step.filter !== undefined && step.filter !== null) {
            qb.walkEdgeFiltered(relId, Number(step.filter));
          } else {
            qb.walkEdge(relId);
          }
        } else {
          qb.walkCsc(relId);
          if (step.filter !== undefined && step.filter !== null) {
            qb.filterNode(Number(step.filter));
          }
        }
      }

      qb.collectBitset();
      const compiled = qb.compile();

      const state = new native.VmState();
      const queryRes = compiled.executeWithContext(ctx, state, this.startNode);
      if (!queryRes.isOk()) {
        return [];
      }

      if (
        queryRes.resultType === native.RegisterType.TYPE_NODE_ID ||
        queryRes.resultType === native.RegisterType.TYPE_INT64
      ) {
        return [Number(queryRes.rawValue)];
      }

      const handle = Number(queryRes.rawValue);
      const wordCount = ctx.bitsetWordCount();
      const results = [];
      for (let w = 0; w < wordCount; w++) {
        let word = ctx.bitsetGetWord(handle, w);
        if (word === 0n) continue;
        const baseId = w * 64;
        for (let b = 0; b < 64 && word !== 0n; b++) {
          if ((word & 1n) !== 0n) {
            results.push(baseId + b);
          }
          word >>= 1n;
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
