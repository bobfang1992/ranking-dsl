import { describe, it, expect } from 'vitest';
import {
  validateTraceKey,
  TRACE_KEY_PATTERN,
} from '../types';

describe('validateTraceKey', () => {
  describe('valid trace keys', () => {
    it('accepts simple alphanumeric keys', () => {
      expect(validateTraceKey('score')).toBeNull();
      expect(validateTraceKey('SCORE')).toBeNull();
      expect(validateTraceKey('score123')).toBeNull();
    });

    it('accepts keys with dots', () => {
      expect(validateTraceKey('score.base')).toBeNull();
      expect(validateTraceKey('feature.embedding.v1')).toBeNull();
    });

    it('accepts keys with underscores', () => {
      expect(validateTraceKey('score_base')).toBeNull();
      expect(validateTraceKey('my_feature_key')).toBeNull();
    });

    it('accepts keys with slashes', () => {
      expect(validateTraceKey('model/v1')).toBeNull();
      expect(validateTraceKey('ns/feature/score')).toBeNull();
    });

    it('accepts keys with hyphens', () => {
      expect(validateTraceKey('score-base')).toBeNull();
      expect(validateTraceKey('my-feature-key')).toBeNull();
    });

    it('accepts mixed charset', () => {
      expect(validateTraceKey('Score_Base.v1/model-2')).toBeNull();
    });

    it('accepts single character keys', () => {
      expect(validateTraceKey('a')).toBeNull();
      expect(validateTraceKey('Z')).toBeNull();
      expect(validateTraceKey('9')).toBeNull();
    });

    it('accepts keys at max length (64 chars)', () => {
      const key64 = 'a'.repeat(64);
      expect(validateTraceKey(key64)).toBeNull();
    });
  });

  describe('invalid trace keys', () => {
    it('rejects empty string', () => {
      const error = validateTraceKey('');
      expect(error).toBe('trace_key must not be empty');
    });

    it('rejects keys exceeding 64 characters', () => {
      const key65 = 'a'.repeat(65);
      const error = validateTraceKey(key65);
      expect(error).toBe('trace_key must be at most 64 characters (got 65)');
    });

    it('rejects keys with spaces', () => {
      const error = validateTraceKey('score base');
      expect(error).toBe('trace_key must only contain [A-Za-z0-9._/-]');
    });

    it('rejects keys with special characters', () => {
      expect(validateTraceKey('score@base')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
      expect(validateTraceKey('score#1')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
      expect(validateTraceKey('score$base')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
      expect(validateTraceKey('score:base')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
    });

    it('rejects keys with unicode characters', () => {
      expect(validateTraceKey('score\u00e9')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
    });

    it('rejects keys with newlines', () => {
      expect(validateTraceKey('score\nbase')).toBe(
        'trace_key must only contain [A-Za-z0-9._/-]'
      );
    });
  });
});

describe('TRACE_KEY_PATTERN', () => {
  it('matches valid keys', () => {
    expect(TRACE_KEY_PATTERN.test('score')).toBe(true);
    expect(TRACE_KEY_PATTERN.test('Score_Base.v1/model-2')).toBe(true);
  });

  it('rejects invalid keys', () => {
    expect(TRACE_KEY_PATTERN.test('')).toBe(false);
    expect(TRACE_KEY_PATTERN.test('a'.repeat(65))).toBe(false);
    expect(TRACE_KEY_PATTERN.test('score base')).toBe(false);
  });
});
