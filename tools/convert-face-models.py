# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "tflite==2.18.0"]
# ///
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 solid174
"""Converts MediaPipe's Face Landmarker models to ncnn for Beautify's face tracking.

    uv run tools/convert-face-models.py

Downloads face_landmarker.task (float16, version 1), checks its SHA-256, and writes
data/models/face-detector.{param,bin} and data/models/face-landmarks.{param,bin}. The weights
stay float16, as Google ships them. docs/architecture.md, "Face tracking", describes the models.
"""
import hashlib
import io
import pathlib
import struct
import urllib.request
import zipfile

import numpy as np
import tflite

TASK_URL = ('https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/'
            'float16/1/face_landmarker.task')
TASK_SHA256 = '64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff'
OUT = pathlib.Path(__file__).resolve().parent.parent / 'data' / 'models'

MODELS = {
    # model file in the task bundle: (output name, {TFLite input or output: ncnn blob})
    'face_detector.tflite': ('face-detector', {
        'input': 'input', 'regressors': 'regressors', 'classificators': 'scores'}),
    'face_landmarks_detector.tflite': ('face-landmarks', {
        'input_12': 'input', 'Identity': 'landmarks', 'Identity_1': 'presence'}),
}

OPS = {v: k for k, v in tflite.BuiltinOperator.__dict__.items() if not k.startswith('_')}
FP16_TAG = 0x01306B47


def same_pads(size, kernel, stride, dilation):
    """TensorFlow's SAME padding: the odd pixel goes after."""
    extent = (kernel - 1) * dilation + 1
    total = max(((size + stride - 1) // stride - 1) * stride + extent - size, 0)
    return total // 2, total - total // 2


class Converter:
    def __init__(self, data, names):
        self.model = tflite.Model.GetRootAsModel(data, 0)
        self.graph = self.model.Subgraphs(0)
        self.names = names
        self.constants = {}
        self.layers = []  # [type, name, inputs, outputs, params, weights]
        self.aliases = {}
        self.pending_pads = {}

    def tensor_name(self, index):
        return self.graph.Tensors(index).Name().decode()

    def shape(self, index):
        return list(self.graph.Tensors(index).ShapeAsNumpy())

    def blob(self, index):
        index = self.aliases.get(index, index)
        return self.names.get(self.tensor_name(index), f't{index}')

    def constant(self, index):
        if index in self.constants:
            return self.constants[index]
        tensor = self.graph.Tensors(index)
        buffer = self.model.Buffers(tensor.Buffer())
        if buffer.DataLength() == 0:
            return None
        dtype = {tflite.TensorType.FLOAT32: np.float32, tflite.TensorType.FLOAT16: np.float16,
                 tflite.TensorType.INT32: np.int32}[tensor.Type()]
        value = np.frombuffer(buffer.DataAsNumpy().tobytes(), dtype).reshape(self.shape(index))
        self.constants[index] = value
        return value

    def add(self, kind, inputs, outputs, params=None, weights=()):
        name = f'{kind.lower()}_{len(self.layers)}'
        self.layers.append([kind, name, inputs, outputs, params or {}, list(weights)])

    def conv_params(self, op_index, x, kernel_h, kernel_w, options):
        top, bottom, left, right = self.pending_pads.pop(op_index, (0, 0, 0, 0))
        _, height, width, _ = self.shape(x)
        if options.Padding() == tflite.Padding.SAME:
            if top or bottom or left or right:
                raise ValueError('explicit padding ahead of a SAME convolution')
            top, bottom = same_pads(height, kernel_h, options.StrideH(), options.DilationHFactor())
            left, right = same_pads(width, kernel_w, options.StrideW(), options.DilationWFactor())
        fused = options.FusedActivationFunction()
        if fused not in (tflite.ActivationFunctionType.NONE, tflite.ActivationFunctionType.RELU):
            raise ValueError(f'unsupported fused activation {fused}')
        return {1: kernel_w, 11: kernel_h, 2: options.DilationWFactor(), 12: options.DilationHFactor(),
                3: options.StrideW(), 13: options.StrideH(), 4: left, 14: top, 15: right, 16: bottom,
                5: 1, 9: 1 if fused == tflite.ActivationFunctionType.RELU else 0}

    def consumers(self, tensor):
        found = []
        for i in range(self.graph.OperatorsLength()):
            op = self.graph.Operators(i)
            if tensor in [op.Inputs(j) for j in range(op.InputsLength())]:
                found.append(i)
        return found

    def opcode(self, op):
        code = self.model.OperatorCodes(op.OpcodeIndex())
        return OPS[max(code.BuiltinCode(), code.DeprecatedBuiltinCode())]

    def convert(self):
        x = self.graph.Inputs(0)
        _, height, width, channels = self.shape(x)
        self.add('Input', [], [self.blob(x)], {0: width, 1: height, 2: channels})
        for op_index in range(self.graph.OperatorsLength()):
            op = self.graph.Operators(op_index)
            kind = self.opcode(op)
            ins = [op.Inputs(j) for j in range(op.InputsLength())]
            out = op.Outputs(0)
            options = op.BuiltinOptions()
            if kind == 'DEQUANTIZE':
                self.constants[out] = self.constant(ins[0])
            elif kind == 'CONV_2D':
                o = tflite.Conv2DOptions()
                o.Init(options.Bytes, options.Pos)
                weight, bias = self.constant(ins[1]), self.constant(ins[2])
                params = self.conv_params(op_index, ins[0], weight.shape[1], weight.shape[2], o)
                params.update({0: weight.shape[0], 6: weight.size})
                self.add('Convolution', [self.blob(ins[0])], [self.blob(out)], params,
                         [(weight.transpose(0, 3, 1, 2), True), (bias, False)])
            elif kind == 'DEPTHWISE_CONV_2D':
                o = tflite.DepthwiseConv2DOptions()
                o.Init(options.Bytes, options.Pos)
                weight, bias = self.constant(ins[1]), self.constant(ins[2])
                if o.DepthMultiplier() != 1:
                    raise ValueError('depth multiplier other than 1')
                params = self.conv_params(op_index, ins[0], weight.shape[1], weight.shape[2], o)
                params.update({0: weight.shape[3], 6: weight.size, 7: weight.shape[3]})
                self.add('ConvolutionDepthWise', [self.blob(ins[0])], [self.blob(out)], params,
                         [(weight[0].transpose(2, 0, 1), True), (bias, False)])
            elif kind == 'PRELU':
                slope = self.constant(ins[1]).reshape(-1)
                self.add('PReLU', [self.blob(ins[0])], [self.blob(out)], {0: slope.size}, [(slope, False)])
            elif kind == 'RELU':
                self.add('ReLU', [self.blob(ins[0])], [self.blob(out)])
            elif kind == 'LOGISTIC':
                self.add('Sigmoid', [self.blob(ins[0])], [self.blob(out)])
            elif kind == 'ADD':
                o = tflite.AddOptions()
                o.Init(options.Bytes, options.Pos)
                fused = o.FusedActivationFunction()
                pair = [self.blob(ins[0]), self.blob(ins[1])]
                if fused == tflite.ActivationFunctionType.NONE:
                    self.add('BinaryOp', pair, [self.blob(out)], {0: 0})
                elif fused == tflite.ActivationFunctionType.RELU:
                    self.add('BinaryOp', pair, [self.blob(out) + '_sum'], {0: 0})
                    self.add('ReLU', [self.blob(out) + '_sum'], [self.blob(out)])
                else:
                    raise ValueError(f'unsupported fused activation {fused}')
            elif kind == 'PAD':
                (_, _), (top, bottom), (left, right), (front, behind) = self.constant(ins[1]).tolist()
                users = self.consumers(out)
                spatial_only = front == 0 and behind == 0
                if spatial_only and len(users) == 1 and self.opcode(self.graph.Operators(users[0])) in (
                        'CONV_2D', 'DEPTHWISE_CONV_2D'):
                    # explicit padding ahead of a VALID convolution goes into the convolution
                    self.pending_pads[users[0]] = (top, bottom, left, right)
                    self.aliases[out] = ins[0]
                else:
                    self.add('Padding', [self.blob(ins[0])], [self.blob(out)],
                             {0: top, 1: bottom, 2: left, 3: right, 4: 0, 5: 0.0, 7: front, 8: behind})
            elif kind == 'MAX_POOL_2D':
                o = tflite.Pool2DOptions()
                o.Init(options.Bytes, options.Pos)
                self.add('Pooling', [self.blob(ins[0])], [self.blob(out)],
                         {0: 0, 1: o.FilterWidth(), 11: o.FilterHeight(), 2: o.StrideW(), 12: o.StrideH(),
                          5: 1 if o.Padding() == tflite.Padding.VALID else 2})
            elif kind == 'RESHAPE':
                before, after = self.shape(ins[0]), self.shape(out)
                if before[1:3] == [1, 1]:
                    # 1×1×C: the channel order already is the element order
                    self.add('Reshape', [self.blob(ins[0])], [self.blob(out)], {0: int(np.prod(after))})
                elif len(after) == 3:
                    # NHWC to (rows, values): ncnn holds CHW, so channels go last first
                    hwc = self.blob(out) + '_hwc'
                    self.add('Permute', [self.blob(ins[0])], [hwc], {0: 3})
                    self.add('Reshape', [hwc], [self.blob(out)], {0: after[2], 1: after[1]})
                else:
                    raise ValueError(f'unsupported reshape {before} -> {after}')
            elif kind == 'CONCATENATION':
                o = tflite.ConcatenationOptions()
                o.Init(options.Bytes, options.Pos)
                if o.Axis() != 1 or len(self.shape(out)) != 3:
                    raise ValueError('unsupported concatenation')
                self.add('Concat', [self.blob(i) for i in ins], [self.blob(out)], {0: 0})
            else:
                raise ValueError(f'unsupported operator {kind}')
        self.prune([self.blob(self.graph.Outputs(i)) for i in range(self.graph.OutputsLength())])
        self.split()

    def prune(self, outputs):
        """Keeps only what the wanted outputs depend on (drops the tongue score)."""
        wanted = {b for b in outputs if b in self.names.values()}
        kept = []
        for layer in reversed(self.layers):
            if set(layer[3]) & wanted:
                kept.append(layer)
                wanted |= set(layer[2])
        self.layers = kept[::-1]

    def split(self):
        """ncnn needs a Split layer wherever one blob feeds several layers."""
        users = {}
        for layer in self.layers:
            for blob in layer[2]:
                users[blob] = users.get(blob, 0) + 1
        taken = {}
        result = []
        for layer in self.layers:
            for i, blob in enumerate(layer[2]):
                if users[blob] > 1:
                    layer[2][i] = f'{blob}_{taken[blob]}'
                    taken[blob] += 1
            result.append(layer)
            for blob in layer[3]:
                if users.get(blob, 0) > 1:
                    taken[blob] = 0
                    result.append(['Split', f'split_{blob}', [blob],
                                   [f'{blob}_{i}' for i in range(users[blob])], {}, []])
        self.layers = result

    def write(self, stem):
        blobs = {b for layer in self.layers for b in layer[3]}
        lines = ['7767517', f'{len(self.layers)} {len(blobs)}']
        weights = io.BytesIO()
        for kind, name, inputs, outputs, params, data in self.layers:
            fields = [f'{kind:<24}', f'{name:<24}', str(len(inputs)), str(len(outputs)), *inputs, *outputs]
            for key, value in params.items():
                fields.append(f'{key}={value:e}' if isinstance(value, float) else f'{key}={value}')
            lines.append(' '.join(fields))
            for array, tagged in data:
                if tagged:
                    raw = np.ascontiguousarray(array, np.float16).tobytes()
                    weights.write(struct.pack('<I', FP16_TAG) + raw + b'\0' * (-len(raw) % 4))
                else:
                    weights.write(np.ascontiguousarray(array, np.float32).tobytes())
        (OUT / f'{stem}.param').write_text('\n'.join(lines) + '\n')
        (OUT / f'{stem}.bin').write_bytes(weights.getvalue())


def main():
    with urllib.request.urlopen(TASK_URL) as response:
        task = response.read()
    digest = hashlib.sha256(task).hexdigest()
    if digest != TASK_SHA256:
        raise SystemExit(f'face_landmarker.task has SHA-256 {digest}, expected {TASK_SHA256}')
    OUT.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(task)) as bundle:
        for member, (stem, names) in MODELS.items():
            converter = Converter(bundle.read(member), names)
            converter.convert()
            converter.write(stem)
            print(f'{stem}: {len(converter.layers)} layers')


if __name__ == '__main__':
    main()
