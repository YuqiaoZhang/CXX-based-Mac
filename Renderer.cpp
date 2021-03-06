#include "Headers_CXX/Foundation_CXX.h"
#include "Headers_CXX/Dispatch_CXX.h"

#include "Renderer.h"
#include "ShaderTypes.h"
#include "TextureLoader/TextureLoader.h"
#include "TextureLoader/MTL/TextureLoader_MTL.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <string>

static const size_t kAlignedUniformsSize = (sizeof(Uniforms) & ~0xFF) + 0x100;

static inline matrix_float4x4 matrix4x4_translation(float tx, float ty, float tz)
{
    return (matrix_float4x4){{{1, 0, 0, 0},
                              {0, 1, 0, 0},
                              {0, 0, 1, 0},
                              {tx, ty, tz, 1}}};
}

static inline matrix_float4x4 matrix4x4_rotation(float radians, vector_float3 axis)
{
    axis = vector_normalize(axis);
    float ct = cosf(radians);
    float st = sinf(radians);
    float ci = 1 - ct;
    float x = axis.x, y = axis.y, z = axis.z;

    return (matrix_float4x4){{{ct + x * x * ci, y * x * ci + z * st, z * x * ci - y * st, 0},
                              {x * y * ci - z * st, ct + y * y * ci, z * y * ci + x * st, 0},
                              {x * z * ci + y * st, y * z * ci - x * st, ct + z * z * ci, 0},
                              {0, 0, 0, 1}}};
}

static inline matrix_float4x4 matrix_perspective_right_hand(float fovyRadians, float aspect, float nearZ, float farZ)
{
    float ys = 1 / tanf(fovyRadians * 0.5);
    float xs = ys / aspect;
    float zs = farZ / (nearZ - farZ);

    return (matrix_float4x4){{{xs, 0, 0, 0},
                              {0, ys, 0, 0},
                              {0, 0, zs, -1},
                              {0, 0, nearZ * zs, 0}}};
}

void demo::_init()
{
    _device = MTLCreateSystemDefaultDevice();

    _width = 800;
    _height = 600;
    CGRect frame = {{0, 0}, {_width, _height}};
    _view = MTKView_initWithFrame(MTKView_alloc(), frame, _device);

    MTKView_setColorPixelFormat(_view, MTLPixelFormatBGRA8Unorm_sRGB);
    MTKView_setDepthStencilPixelFormat(_view, MTLPixelFormatDepth32Float);
    MTKView_setSampleCount(_view, 1);
}

#include <dirent.h>

void demo::_init2()
{
    _commandQueue = MTLDevice_newCommandQueue(_device);

    //pipelineState
    struct MTLVertexDescriptor *vertexdescriptor = MTLVertexDescriptor_init(MTLVertexDescriptor_alloc());
    MTLVertexBufferLayoutDescriptor_setStride(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), 12);
    MTLVertexBufferLayoutDescriptor_setStepFunction(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), MTLVertexStepFunctionPerVertex);
    MTLVertexBufferLayoutDescriptor_setStepRate(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), 1);

    MTLVertexBufferLayoutDescriptor_setStride(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), 12);
    MTLVertexBufferLayoutDescriptor_setStepFunction(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), MTLVertexStepFunctionPerVertex);
    MTLVertexBufferLayoutDescriptor_setStepRate(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshPositions), 1);

    MTLVertexBufferLayoutDescriptor_setStride(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshGenerics), 8);
    MTLVertexBufferLayoutDescriptor_setStepFunction(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshGenerics), MTLVertexStepFunctionPerVertex);
    MTLVertexBufferLayoutDescriptor_setStepRate(MTLVertexBufferLayoutDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_layouts(vertexdescriptor), BufferIndexMeshGenerics), 1);

    MTLVertexAttributeDescriptor_setFormat(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributePosition), MTLVertexFormatFloat3);
    MTLVertexAttributeDescriptor_setOffset(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributePosition), 0);
    MTLVertexAttributeDescriptor_setBufferIndex(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributePosition), BufferIndexMeshPositions);

    MTLVertexAttributeDescriptor_setFormat(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributeTexcoord), MTLVertexFormatFloat2);
    MTLVertexAttributeDescriptor_setOffset(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributeTexcoord), 0);
    MTLVertexAttributeDescriptor_setBufferIndex(MTLVertexAttributeDescriptorArray_objectAtIndexedSubscript(MTLVertexDescriptor_attributes(vertexdescriptor), VertexAttributeTexcoord), BufferIndexMeshGenerics);

    size_t buffersize;
    void *buffer;
    {
        std::string metallib_filename;
           metallib_filename = NSURL_fileSystemRepresentation(NSArrayNSURL_objectAtIndexedSubscript(NSFileManager_URLsForDirectory(NSFileManager_defaultManager(), NSCachesDirectory, NSUserDomainMask), 0));
           metallib_filename += "/Shaders.metallib";
        
        int fd_metallib = openat(AT_FDCWD, metallib_filename.c_str(), O_RDONLY);

        struct stat stbuf;
        int res = fstat(fd_metallib, &stbuf);
        assert(res == 0 && S_ISREG(stbuf.st_mode));

        buffersize = stbuf.st_size;
        buffer = malloc(buffersize);
        ssize_t res2 = read(fd_metallib, buffer, buffersize);
        assert(res2 != -1 && res2 == buffersize);

        uint8_t buf_u_assert_only[1];
        ssize_t res3 = read(fd_metallib, buf_u_assert_only, 1);
        assert(res3 != -1 && res3 == 0);

        int res4 = close(fd_metallib);
        assert(res4 == 0);
    }

    dispatch_queue_main_t mainqueue = dispatch_get_main_queue();
    dispatch_data_t dispathdata = dispatch_data_create(buffer, buffersize, mainqueue, NULL, [](void *, void *buffer) -> void {
        free(buffer);
    });

    struct NSError *error1 = NULL;
    struct MTLLibrary *myLibrary = MTLDevice_newLibraryWithData(_device, dispathdata, &error1);
    dispatch_release(dispathdata);

    struct MTLFunction *vertexFunction = MTLLibrary_newFunctionWithName(myLibrary, "vertexShader");
    struct MTLFunction *fragmentFunction = MTLLibrary_newFunctionWithName(myLibrary, "fragmentShader");

    struct MTLRenderPipelineDescriptor *pipelineStateDescriptor = MTLRenderPipelineDescriptor_init(MTLRenderPipelineDescriptor_alloc());
    MTLRenderPipelineDescriptor_setLabel(pipelineStateDescriptor, "MyPipeline");
    MTLRenderPipelineDescriptor_setSampleCount(pipelineStateDescriptor, 1);
    MTLRenderPipelineDescriptor_setVertexFunction(pipelineStateDescriptor, vertexFunction);
    MTLRenderPipelineDescriptor_setFragmentFunction(pipelineStateDescriptor, fragmentFunction);
    MTLRenderPipelineDescriptor_setVertexDescriptor(pipelineStateDescriptor, vertexdescriptor);
    MTLRenderPipelineColorAttachmentDescriptor_setPixelFormat(MTLRenderPipelineDescriptor_colorAttachmentAt(pipelineStateDescriptor, 0), MTLPixelFormatBGRA8Unorm_sRGB);
    MTLRenderPipelineDescriptor_setDepthAttachmentPixelFormat(pipelineStateDescriptor, MTLPixelFormatDepth32Float);
    MTLRenderPipelineDescriptor_setStencilAttachmentPixelFormat(pipelineStateDescriptor, MTLPixelFormatInvalid);
    MTLFunction_release(vertexFunction);
    MTLFunction_release(fragmentFunction);
    MTLLibrary_release(myLibrary);
    MTLVertexDescriptor_release(vertexdescriptor);

    struct NSError *error = NULL;
    _pipelineState = MTLDevice_newRenderPipelineStateWithDescriptor(_device, pipelineStateDescriptor, &error);
    MTLRenderPipelineDescriptor_release(pipelineStateDescriptor);

    //depthState
    struct MTLDepthStencilDescriptor *depthStateDesc = MTLDepthStencilDescriptor_init(MTLDepthStencilDescriptor_alloc());
    MTLDepthStencilDescriptor_setDepthCompareFunction(depthStateDesc, MTLCompareFunctionLess);
    MTLDepthStencilDescriptor_setDepthWriteEnabled(depthStateDesc, true);

    _depthState = MTLDevice_newDepthStencilStateWithDescriptor(_device, depthStateDesc);
    MTLDepthStencilDescriptor_release(depthStateDesc);

    _uniformBufferIndex = 0;
    for (NSUInteger i = 0; i < kMaxBuffersInFlight; ++i)
    {
        _inFlightSemaphore[i].CreateManualEventNoThrow(true);
    }

    NSUInteger uniformBufferSize = kAlignedUniformsSize * kMaxBuffersInFlight;
    _dynamicUniformBuffer = MTLDevice_newBufferWithLength(_device, uniformBufferSize, MTLResourceCPUCacheModeWriteCombined | MTLResourceStorageModeShared);
    MTLBuffer_setLabel(_dynamicUniformBuffer, "UniformBuffer");

    float aspect = _width / _height;
    _projectionMatrix = matrix_perspective_right_hand(65.0f * (M_PI / 180.0f), aspect, 0.1f, 100.0f);

    _rotation = 0.0f;

    //--------------------------------------------------------------------------------------
    // Mesh and VertexFormat Data
    //--------------------------------------------------------------------------------------
    // clang-format off
      float const g_vertex_buffer_data[] = {
          -1.0f,-1.0f,-1.0f,  // -X side
          -1.0f,-1.0f, 1.0f,
          -1.0f, 1.0f, 1.0f,
          -1.0f, 1.0f, 1.0f,
          -1.0f, 1.0f,-1.0f,
          -1.0f,-1.0f,-1.0f,
          
          -1.0f,-1.0f,-1.0f,  // -Z side
          1.0f, 1.0f,-1.0f,
          1.0f,-1.0f,-1.0f,
          -1.0f,-1.0f,-1.0f,
          -1.0f, 1.0f,-1.0f,
          1.0f, 1.0f,-1.0f,
          
          -1.0f,-1.0f,-1.0f,  // -Y side
          1.0f,-1.0f,-1.0f,
          1.0f,-1.0f, 1.0f,
          -1.0f,-1.0f,-1.0f,
          1.0f,-1.0f, 1.0f,
          -1.0f,-1.0f, 1.0f,
          
          -1.0f, 1.0f,-1.0f,  // +Y side
          -1.0f, 1.0f, 1.0f,
          1.0f, 1.0f, 1.0f,
          -1.0f, 1.0f,-1.0f,
          1.0f, 1.0f, 1.0f,
          1.0f, 1.0f,-1.0f,
          
          1.0f, 1.0f,-1.0f,  // +X side
          1.0f, 1.0f, 1.0f,
          1.0f,-1.0f, 1.0f,
          1.0f,-1.0f, 1.0f,
          1.0f,-1.0f,-1.0f,
          1.0f, 1.0f,-1.0f,
          
          -1.0f, 1.0f, 1.0f,  // +Z side
          -1.0f,-1.0f, 1.0f,
          1.0f, 1.0f, 1.0f,
          -1.0f,-1.0f, 1.0f,
          1.0f,-1.0f, 1.0f,
          1.0f, 1.0f, 1.0f,
      };
      
      float const g_uv_buffer_data[] = {
          0.0f, 1.0f,  // -X side
          1.0f, 1.0f,
          1.0f, 0.0f,
          1.0f, 0.0f,
          0.0f, 0.0f,
          0.0f, 1.0f,
          
          1.0f, 1.0f,  // -Z side
          0.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 1.0f,
          1.0f, 0.0f,
          0.0f, 0.0f,
          
          1.0f, 0.0f,  // -Y side
          1.0f, 1.0f,
          0.0f, 1.0f,
          1.0f, 0.0f,
          0.0f, 1.0f,
          0.0f, 0.0f,
          
          1.0f, 0.0f,  // +Y side
          0.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 1.0f,
          
          1.0f, 0.0f,  // +X side
          0.0f, 0.0f,
          0.0f, 1.0f,
          0.0f, 1.0f,
          1.0f, 1.0f,
          1.0f, 0.0f,
          
          0.0f, 0.0f,  // +Z side
          0.0f, 1.0f,
          1.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 1.0f,
          1.0f, 0.0f,
      };
    // clang-format on

    _meshvertexBuffer = MTLDevice_newBufferWithLength(_device, sizeof(g_vertex_buffer_data), MTLResourceStorageModeShared);
    memcpy(MTLBuffer_contents(_meshvertexBuffer), g_vertex_buffer_data, sizeof(g_vertex_buffer_data));

    _meshvertexBuffer_Addition = MTLDevice_newBufferWithLength(_device, sizeof(g_uv_buffer_data), MTLResourceStorageModeShared);
    memcpy(MTLBuffer_contents(_meshvertexBuffer_Addition), g_uv_buffer_data, sizeof(g_uv_buffer_data));

    //Load Texture
    _stagingBuffer = MTLDevice_newBufferWithLength(_device, 1024 * 1024 * 70, MTLResourceStorageModeShared);
    MTLBuffer_setLabel(_stagingBuffer, "StagingBuffer");

    std::string tex_filename;
    tex_filename = NSURL_fileSystemRepresentation(NSArrayNSURL_objectAtIndexedSubscript(NSFileManager_URLsForDirectory(NSFileManager_defaultManager(), NSCachesDirectory, NSUserDomainMask), 0));
    tex_filename += "/l_hires";
    struct TextureLoader_NeutralHeader header;
    size_t header_offset = 0;
    TextureLoader_LoadHeaderFromFile(tex_filename.c_str(), &header, &header_offset);

    struct TextureLoader_SpecificHeader mtlheader = TextureLoader_ToSpecificHeader(&header);

    struct MTLTextureDescriptor *textureDesc = MTLTextureDescriptor_init(MTLTextureDescriptor_alloc());
    MTLTextureDescriptor_setTextureType(textureDesc, mtlheader.textureType);
    MTLTextureDescriptor_setPixelFormat(textureDesc, mtlheader.pixelFormat);
    MTLTextureDescriptor_setWidth(textureDesc, mtlheader.width);
    MTLTextureDescriptor_setHeight(textureDesc, mtlheader.height);
    MTLTextureDescriptor_setDepth(textureDesc, mtlheader.depth);
    MTLTextureDescriptor_setMipmapLevelCount(textureDesc, mtlheader.mipmapLevelCount);
    MTLTextureDescriptor_setArrayLength(textureDesc, mtlheader.arrayLength);
    MTLTextureDescriptor_setSampleCount(textureDesc, 1);
    MTLTextureDescriptor_setResourceOptions(textureDesc, MTLResourceStorageModePrivate);
    MTLTextureDescriptor_setUsage(textureDesc, MTLTextureUsageShaderRead);

    _colorMap = MTLDevice_newTextureWithDescriptor(_device, textureDesc);
    MTLTextureDescriptor_release(textureDesc);
    MTLTexture_setLabel(_colorMap, "ColorMap");

    uint32_t NumSubresource = TextureLoader_GetFormatAspectCount(mtlheader.pixelFormat) * TextureLoader_GetSliceCount(mtlheader.textureType, mtlheader.arrayLength) * mtlheader.mipmapLevelCount;

    struct TextureLoader_MemcpyDest dest[15];
    struct TextureLoader_MTLCopyFromBuffer regions[15];
    size_t TotalSize = TextureLoader_GetCopyableFootprints(&mtlheader, NumSubresource, dest, regions);

    assert(TotalSize < (1024 * 1024 * 70));
    TextureLoader_FillDataFromFile(tex_filename.c_str(), static_cast<uint8_t *>(MTLBuffer_contents(_stagingBuffer)), NumSubresource, dest, &header, &header_offset);

    struct MTLCommandBuffer *commandPool = MTLCommandQueue_commandBuffer(_commandQueue);
    MTLCommandBuffer_setLabel(commandPool, "TextureLoader");

    struct MTLBlitCommandEncoder *commandBuffer = MTLCommandBuffer_blitCommandEncoder(commandPool);
    MTLBlitCommandEncoder_setLabel(commandBuffer, "TextureLoader");
    for (uint32_t i = 0; i < NumSubresource; ++i)
    {
        MTLBlitCommandEncoder_copyFromBuffer(commandBuffer, _stagingBuffer, regions[i].sourceOffset, regions[i].sourceBytesPerRow, regions[i].sourceBytesPerImage, regions[i].sourceSize, _colorMap, regions[i].destinationSlice, regions[i].destinationLevel, regions[i].destinationOrigin);
    }
    MTLBlitCommandEncoder_endEncoding(commandBuffer);

    _event_colorMap.CreateManualEventNoThrow(false);

    MTLCommandBuffer_addCompletedHandler(commandPool, &_event_colorMap, 0, [](void *pUserData, NSUInteger throttlingIndex, struct MTLCommandBuffer *) -> void {
        static_cast<GCEvent *>(pUserData)->Set();
    });
    MTLCommandBuffer_commit(commandPool);

    //multi-thread
    _workerTheadArg[0]._exit = false;
    _workerTheadArg[0]._eventwait.CreateAutoEventNoThrow(false);
    _workerTheadArg[0]._eventsignal.CreateAutoEventNoThrow(false);
    _workerTheadArg[0]._secondarycmd = NULL;
    _workerTheadArg[0]._self = this;

    int res1 = pthread_create(&_workerThread[0], NULL, _workThreadMain, &_workerTheadArg[0]);
    assert(res1 == 0);
}

void *demo::_workThreadMain(void *arg)
{
    while (!static_cast<struct WorkerTheadArg *>(arg)->_exit)
    {
        static_cast<struct WorkerTheadArg *>(arg)->_eventwait.Wait(INFINITE, false);

        uint32_t uniformBufferOffset = kAlignedUniformsSize * static_cast<struct WorkerTheadArg *>(arg)->_self->_uniformBufferIndex;

        struct MTLRenderCommandEncoder *renderEncoder = static_cast<struct WorkerTheadArg *>(arg)->_secondarycmd;

        MTLRenderCommandEncoder_pushDebugGroup(renderEncoder, "DrawBox");

        MTLRenderCommandEncoder_setFrontFacingWinding(renderEncoder, MTLWindingCounterClockwise);
        MTLRenderCommandEncoder_setCullMode(renderEncoder, MTLCullModeBack);
        MTLRenderCommandEncoder_setRenderPipelineState(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_pipelineState);
        MTLRenderCommandEncoder_setDepthStencilState(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_depthState);

        MTLRenderCommandEncoder_setVertexBuffer(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_dynamicUniformBuffer, uniformBufferOffset, BufferIndexUniforms);
        MTLRenderCommandEncoder_setFragmentBuffer(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_dynamicUniformBuffer, uniformBufferOffset, BufferIndexUniforms);
        MTLRenderCommandEncoder_setFragmentTexture(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_colorMap, TextureIndexColor);
        MTLRenderCommandEncoder_setVertexBuffer(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_meshvertexBuffer, 0, BufferIndexMeshPositions);
        MTLRenderCommandEncoder_setVertexBuffer(renderEncoder, static_cast<struct WorkerTheadArg *>(arg)->_self->_meshvertexBuffer_Addition, 0, BufferIndexMeshGenerics);

        MTLRenderCommandEncoder_drawPrimitives(renderEncoder, MTLPrimitiveTypeTriangle, 0, 12 * 3, 1, 0);

        MTLRenderCommandEncoder_popDebugGroup(renderEncoder);
        MTLRenderCommandEncoder_endEncoding(renderEncoder);

        static_cast<struct WorkerTheadArg *>(arg)->_secondarycmd = NULL;

        static_cast<struct WorkerTheadArg *>(arg)->_eventsignal.Set();
    }

    return NULL;
}

void demo::_resize(float width, float height)
{
    float aspect = _width / _height;
    _projectionMatrix = matrix_perspective_right_hand(65.0f * (M_PI / 180.0f), aspect, 0.1f, 100.0f);
}

void demo::_draw(struct MTKView *view)
{
    /// Per frame updates here
    _uniformBufferIndex = (_uniformBufferIndex + 1) % kMaxBuffersInFlight;

    _inFlightSemaphore[_uniformBufferIndex].Wait(INFINITE, false);

    vector_float3 rotationAxis = {1, 1, 0};
    matrix_float4x4 modelMatrix = matrix4x4_rotation(_rotation, rotationAxis);
    matrix_float4x4 viewMatrix = matrix4x4_translation(0.0, 0.0, -8.0);
    _rotation += 0.01f;

    uint32_t uniformBufferOffset = kAlignedUniformsSize * _uniformBufferIndex;
    void *uniformBufferAddress = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(MTLBuffer_contents(_dynamicUniformBuffer)) + uniformBufferOffset);
    Uniforms *uniforms = static_cast<Uniforms *>(uniformBufferAddress);
    uniforms->projectionMatrix = _projectionMatrix;
    uniforms->modelViewMatrix = matrix_multiply(viewMatrix, modelMatrix);

    struct MTLCommandBuffer *commandPool = MTLCommandQueue_commandBuffer(_commandQueue);
    MTLCommandBuffer_setLabel(commandPool, "MyCommand");

    MTLCommandBuffer_addCompletedHandler(commandPool, this, _uniformBufferIndex, [](void *pUserData, NSUInteger throttlingIndex, struct MTLCommandBuffer *buffer) -> void {
        static_cast<struct demo *>(pUserData)->_inFlightSemaphore[throttlingIndex].Set();
    });

    /// Delay getting the currentRenderPassDescriptor until we absolutely need it to avoid
    ///   holding onto the drawable and blocking the display pipeline any longer than necessary
    struct MTLRenderPassDescriptor *renderPassDescriptor = MTKView_currentRenderPassDescriptor(view);
    if (NULL != renderPassDescriptor)
    {
        if (!_init_colorMap)
        {
            _init_colorMap = true;
            _event_colorMap.Wait(INFINITE, false);
        }

        /// Final pass rendering code here

        struct MTLParallelRenderCommandEncoder *primarycmd = MTLCommandBuffer_parallelRenderCommandEncoderWithDescriptor(commandPool, renderPassDescriptor);
        MTLParallelRenderCommandEncoder_setLabel(primarycmd, "MyPrimaryRenderEncoder");

        assert(_workerTheadArg[0]._secondarycmd == NULL);
        _workerTheadArg[0]._secondarycmd = MTLParallelRenderCommandEncoder_renderCommandEncoder(primarycmd);
        MTLRenderCommandEncoder_setLabel(_workerTheadArg[0]._secondarycmd, "MySecondaryRenderEncoder");
        _workerTheadArg[0]._eventwait.Set();

        _workerTheadArg[0]._eventsignal.Wait(INFINITE, false);

        MTLParallelRenderCommandEncoder_endEncoding(primarycmd);

        MTLCommandBuffer_presentDrawable(commandPool, MTKView_currentDrawable(view));
    }

    _inFlightSemaphore[_uniformBufferIndex].Reset();
    MTLCommandBuffer_commit(commandPool);
}
