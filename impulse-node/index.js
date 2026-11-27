'use strict';

const native = require('./build/Release/impulse_node_native.node');

const POW10_BIG = Array.from({ length: 19 }, (_, i) => 10n ** BigInt(i));
const INT64_MIN = -(1n << 63n);
const INT64_MAX = (1n << 63n) - 1n;

function validatePrecisionScale(precision, scale) {
  if (!Number.isInteger(precision) || precision < 1 || precision > 18) {
    throw new RangeError(`DECIMAL64 precision must be an integer in [1, 18], got ${precision}`);
  }
  if (!Number.isInteger(scale) || scale < 0 || scale > precision) {
    throw new RangeError(
      `DECIMAL64 scale must be an integer in [0, ${precision}], got ${scale}`
    );
  }
}

class Decimal64 {
  constructor(unscaled, precision = 18, scale = 2) {
    validatePrecisionScale(precision, scale);
    let val;
    if (typeof unscaled === 'bigint') {
      val = unscaled;
    } else if (typeof unscaled === 'number' && Number.isInteger(unscaled)) {
      if (!Number.isSafeInteger(unscaled)) {
        throw new RangeError(
          `Number ${unscaled} exceeds safe integer range; pass a BigInt for > 2^53 precision`
        );
      }
      val = BigInt(unscaled);
    } else {
      throw new TypeError('Decimal64 unscaled must be a bigint or safe integer');
    }
    if (val < INT64_MIN || val > INT64_MAX) {
      throw new RangeError(`Decimal64 unscaled ${val} out of signed 64-bit integer range`);
    }
    const limit = POW10_BIG[precision];
    const absVal = val < 0n ? -val : val;
    if (absVal >= limit) {
      throw new RangeError(
        `Decimal64 unscaled ${val} exceeds DECIMAL(${precision}, ${scale}) bounds`
      );
    }
    this.unscaled = val;
    this.precision = precision;
    this.scale = scale;
    Object.freeze(this);
  }

  static fromUnscaled(unscaled, precision = 18, scale = 2) {
    return new Decimal64(unscaled, precision, scale);
  }

  static fromString(str, precision = 18, scale = 2) {
    validatePrecisionScale(precision, scale);
    if (typeof str !== 'string') {
      throw new TypeError('Decimal64.fromString expects a string');
    }
    const s = str.trim();
    if (s.length === 0) {
      throw new SyntaxError('Empty DECIMAL64 string');
    }

    let idx = 0;
    let neg = false;
    if (s[0] === '-') {
      neg = true;
      idx = 1;
    } else if (s[0] === '+') {
      idx = 1;
    }
    if (idx >= s.length) {
      throw new SyntaxError(`Malformed DECIMAL64 literal: '${str}'`);
    }

    let intPart = '';
    let fracPart = '';
    let seenDot = false;
    let seenDigit = false;

    for (; idx < s.length; idx++) {
      const ch = s[idx];
      if (ch === '.') {
        if (seenDot) {
          throw new SyntaxError(`Multiple decimal points in DECIMAL64 literal: '${str}'`);
        }
        seenDot = true;
      } else if (ch >= '0' && ch <= '9') {
        seenDigit = true;
        if (seenDot) {
          fracPart += ch;
        } else {
          intPart += ch;
        }
      } else {
        throw new SyntaxError(`Invalid character '${ch}' in DECIMAL64 literal: '${str}'`);
      }
    }

    if (!seenDigit) {
      throw new SyntaxError(`No digits in DECIMAL64 literal: '${str}'`);
    }

    let roundUp = false;
    let keptFrac;
    if (fracPart.length <= scale) {
      keptFrac = fracPart.padEnd(scale, '0');
    } else {
      keptFrac = fracPart.slice(0, scale);
      // Half-away-from-zero rounding
      roundUp = fracPart[scale] >= '5';
    }

    const digits = (intPart || '0') + keptFrac;
    let mag = BigInt(digits);
    if (roundUp) {
      mag += 1n;
    }

    const limit = POW10_BIG[precision];
    if (mag >= limit) {
      throw new RangeError(
        `DECIMAL64 literal '${str}' overflows DECIMAL(${precision}, ${scale})`
      );
    }

    const unscaled = neg && mag !== 0n ? -mag : mag;
    return new Decimal64(unscaled, precision, scale);
  }

  toString() {
    if (this.scale === 0) {
      return this.unscaled.toString();
    }
    const neg = this.unscaled < 0n;
    const digits = (neg ? -this.unscaled : this.unscaled).toString();
    const prefix = neg ? '-' : '';
    if (digits.length <= this.scale) {
      return `${prefix}0.${'0'.repeat(this.scale - digits.length)}${digits}`;
    }
    const split = digits.length - this.scale;
    return `${prefix}${digits.slice(0, split)}.${digits.slice(split)}`;
  }

  toNumber() {
    return Number(this.unscaled) / 10 ** this.scale;
  }

  equals(other) {
    if (!(other instanceof Decimal64)) {
      return false;
    }
    if (this.scale === other.scale) {
      return this.unscaled === other.unscaled;
    }
    if (this.scale < other.scale) {
      return this.unscaled * POW10_BIG[other.scale - this.scale] === other.unscaled;
    }
    return this.unscaled === other.unscaled * POW10_BIG[this.scale - other.scale];
  }
}

function packDecimal64Column(values, precision, scale, nullable = false) {
  validatePrecisionScale(precision, scale);
  if (Buffer.isBuffer(values) || ArrayBuffer.isView(values)) {
    return values;
  }
  if (!Array.isArray(values)) {
    throw new TypeError('values must be an Array, Buffer, or TypedArray');
  }
  const n = values.length;
  const unscaled = new BigInt64Array(n);
  let validWords = null;
  let paddedBmBytes = 0;

  if (nullable) {
    const wordCount = Math.ceil(n / 64);
    validWords = new BigUint64Array(wordCount);
    const rawBmBytes = wordCount * 8;
    paddedBmBytes = (rawBmBytes + 127) & ~127;
  }

  for (let i = 0; i < n; i++) {
    const v = values[i];
    if (v === null || v === undefined) {
      if (!nullable) {
        throw new TypeError(`Null value at index ${i} for non-nullable DECIMAL64 column`);
      }
      unscaled[i] = 0n;
      continue;
    }
    if (nullable) {
      validWords[i >> 6] |= 1n << BigInt(i & 63);
    }
    if (v instanceof Decimal64) {
      if (v.scale === scale) {
        unscaled[i] = v.unscaled;
      } else {
        unscaled[i] = Decimal64.fromString(v.toString(), precision, scale).unscaled;
      }
    } else if (typeof v === 'string') {
      unscaled[i] = Decimal64.fromString(v, precision, scale).unscaled;
    } else if (typeof v === 'bigint') {
      unscaled[i] = new Decimal64(v, precision, scale).unscaled;
    } else if (typeof v === 'number') {
      unscaled[i] = new Decimal64(v, precision, scale).unscaled;
    } else {
      throw new TypeError(`Unsupported DECIMAL64 value at index ${i}`);
    }
  }

  if (!nullable) {
    return unscaled;
  }

  const out = Buffer.alloc(paddedBmBytes + n * 8);
  if (validWords.byteLength > 0) {
    Buffer.from(validWords.buffer, validWords.byteOffset, validWords.byteLength).copy(out, 0);
  }
  if (unscaled.byteLength > 0) {
    Buffer.from(unscaled.buffer, unscaled.byteOffset, unscaled.byteLength).copy(
      out,
      paddedBmBytes
    );
  }
  return out;
}

function decodeDecimal64Values(raw) {
  const { precision, scale, isNullable, unscaled, validBits } = raw;
  const out = new Array(unscaled.length);
  for (let i = 0; i < unscaled.length; i++) {
    if (isNullable && validBits) {
      const bit = (validBits[i >> 6] >> BigInt(i & 63)) & 1n;
      if (bit === 0n) {
        out[i] = null;
        continue;
      }
    }
    out[i] = new Decimal64(unscaled[i], precision, scale);
  }
  return out;
}

native.Writer.prototype.addDomainDecimal64 = function (
  domainIndex,
  name,
  precision,
  scale,
  values,
  options = {}
) {
  const nullable = Boolean(options.nullable);
  const dimension = options.dimension || 1;
  const typeCode = nullable ? 0x95 : 0x15;
  const packed = packDecimal64Column(values, precision, scale, nullable);
  this.addDomainAttributeDecimal64(
    domainIndex,
    name,
    precision,
    scale,
    packed,
    dimension,
    typeCode
  );
};

native.Writer.prototype.addRelationDecimal64 = function (
  relationIndex,
  name,
  precision,
  scale,
  values,
  options = {}
) {
  const nullable = Boolean(options.nullable);
  const dimension = options.dimension || 1;
  const typeCode = nullable ? 0x95 : 0x15;
  const packed = packDecimal64Column(values, precision, scale, nullable);
  this.addAttributeDecimal64(relationIndex, name, precision, scale, packed, dimension, typeCode);
};

native.Snapshot.prototype.getDomainDecimal64 = function (domainIndex, attrIndex) {
  const raw = this.getDomainAttributeDecimal64(domainIndex, attrIndex);
  return decodeDecimal64Values(raw);
};

native.Snapshot.prototype.getRelationDecimal64 = function (relationIndex, attrIndex) {
  const raw = this.getAttributeDecimal64(relationIndex, attrIndex);
  return decodeDecimal64Values(raw);
};

class CompiledV11Program {
  constructor(compiled) {
    this.impbBytes = compiled.impbBytes;
    this.paramNameToId = { ...compiled.paramNameToId };
    this.paramSchema = compiled.paramSchema.map((p) => ({ ...p }));
    this.outputNames = [...compiled.outputNames];
    this.outputSchema = compiled.outputSchema.map((o) => ({ ...o }));
    this.impscmSource = compiled.impscmSource || null;
  }

  execute(snapshot = null, params = {}, scratchBytes = 4 * 1024 * 1024) {
    const bindings = [];
    for (const pm of this.paramSchema) {
      const name = pm.name;
      const bare = name.startsWith('$') ? name.slice(1) : name;
      let val;
      if (Object.prototype.hasOwnProperty.call(params, name)) {
        val = params[name];
      } else if (Object.prototype.hasOwnProperty.call(params, bare)) {
        val = params[bare];
      } else {
        throw new Error(`Missing required query parameter '${name}'`);
      }

      const paramId = pm.paramId;
      if (val === null || val === undefined) {
        bindings.push({ paramId, kind: 0 });
      } else if (pm.isDecimal) {
        let scaled;
        if (val instanceof Decimal64) {
          scaled =
            val.scale === pm.scale
              ? val.unscaled
              : Decimal64.fromString(val.toString(), pm.precision, pm.scale).unscaled;
        } else if (typeof val === 'string') {
          scaled = Decimal64.fromString(val, pm.precision, pm.scale).unscaled;
        } else if (typeof val === 'bigint') {
          scaled = new Decimal64(val, pm.precision, pm.scale).unscaled;
        } else {
          scaled = Decimal64.fromString(String(val), pm.precision, pm.scale).unscaled;
        }
        bindings.push({ paramId, kind: 1, scalarI64: scaled });
      } else if (pm.type === 'ID' || pm.type === 'U64') {
        bindings.push({ paramId, kind: 2, scalarBits: BigInt(val) });
      } else if (pm.type === 'I64') {
        bindings.push({ paramId, kind: 1, scalarI64: BigInt(val) });
      } else if (pm.type === 'F64') {
        bindings.push({ paramId, kind: 3, scalarF64: Number(val) });
      } else if (pm.type === 'BOOL') {
        bindings.push({ paramId, kind: 4, scalarBits: val ? 1n : 0n });
      } else if (pm.type === 'STR') {
        bindings.push({ paramId, kind: 5, strVal: String(val) });
      } else if (pm.type === 'FRONTIER') {
        bindings.push({
          paramId,
          kind: 6,
          domainId: pm.domainId,
          ids: Array.from(val, (x) => BigInt(x)),
        });
      } else {
        bindings.push({ paramId, kind: 1, scalarI64: BigInt(val) });
      }
    }

    const ws = new native.VmWorkspace(scratchBytes);
    try {
      const rc = ws.executeParams(this.impbBytes, snapshot, bindings);
      if (rc !== native.VmStatus.OK) {
        throw new Error(`ImpulseVM v0.11 execution failed with status=${rc}`);
      }
      const outputs = {};
      for (let slot = 0; slot < this.outputSchema.length; slot++) {
        const meta = this.outputSchema[slot];
        const rawOut = ws.getOutput(slot);
        if (!rawOut) {
          outputs[meta.name] = null;
          continue;
        }
        if (meta.isDecimal) {
          const i64Arr = rawOut.i64Data || [];
          const decVals = i64Arr.map((v) =>
            v === null ? null : new Decimal64(v, meta.precision || 18, meta.scale)
          );
          outputs[meta.name] = decVals.length === 1 ? decVals[0] : decVals;
        } else {
          const data = rawOut.data || [];
          outputs[meta.name] = data.length === 1 ? data[0] : data;
        }
      }
      return outputs;
    } finally {
      ws.destroy();
    }
  }
}

function compileImpScm(source, snapshot = null, enableOptimizations = true) {
  return new CompiledV11Program(native.compileImpScm(source, snapshot, enableOptimizations));
}

function compileCypher(cypher, snapshot = null, paramTypes = {}, enableOptimizations = true) {
  return new CompiledV11Program(
    native.compileCypher(cypher, snapshot, paramTypes, enableOptimizations)
  );
}

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

const Opcodes = {
  ...(native.Opcodes || {}),
  SCALAR_DECIMAL_ALU: 0x15,
  VEC_DECIMAL_ALU: 0x58,
};

const VmStatus = {
  ...(native.VmStatus || {}),
  ARITHMETIC_OVERFLOW: 16,
  DIV_BY_ZERO: 17,
  ERR_ARITHMETIC_OVERFLOW: 16,
  ERR_DIV_BY_ZERO: 17,
};

const AttrType = {
  ...(native.AttrType || {}),
  BOOL: 0x0f,
  DECIMAL64: 0x15,
  DECIMAL64_NULLABLE: 0x95,
};

module.exports = {
  ...native,
  Decimal64,
  CompiledV11Program,
  compileImpScm,
  compileCypher,
  Opcodes,
  VmStatus,
  AttrType,
  Traversal,
};
