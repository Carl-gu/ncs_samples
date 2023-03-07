from flask.app import Flask, Response
from flask.helpers import send_from_directory, send_file, flash, url_for
from flask.templating import render_template
from flask.globals import request
from flask.helpers import make_response
from werkzeug.utils import secure_filename, redirect
from urllib.parse import urlparse
import re
import os
import time
import mimetypes



UPLOAD_FOLDER = '/root/www/nrf91/upload'
ALLOWED_EXTENSIONS = {'zip', 'bin'}


app = Flask(__name__)
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_PATH'] = 1024 * 1024  # Max file size is 1M


# This code is referred to https://wil.dog/2016/09/07/streaming-with-flask/
def partial_response(path, start, end=None):
    file_size = os.path.getsize(path)

    if end is None:
        end = file_size - start - 1
    end = min(end, file_size - 1)
    length = end - start + 1

    print('start: {}, end: {}, len: {}'.format(start, end, length))

    with open(path, 'rb') as fd:
        fd.seek(start)
        _bytes = fd.read(length)

    response = Response(
        _bytes,
        206,                                     # Partial Content
        mimetype=mimetypes.guess_type(path)[0],  # Content-Type must be correct
        direct_passthrough=True,                 # Identity encoding
    )
    response.headers.add(
        'Content-Range', 'bytes {0}-{1}/{2}'.format(
            start, end, file_size,
        ),
    )
    response.headers.add(
        'Accept-Ranges', 'bytes'                # Accept request with Range header
    )
    return response


def get_range(req):
    _range = req.headers.get('Range')
    m = re.match(r'bytes=(?P<start>\d+)-(?P<end>\d+)?', _range)
    if m:
        start = m.group('start')
        end = m.group('end')
        start = int(start)
        if end is not None:
            end = int(end)
        return start, end
    else:
        return 0, None


def download_file(path):
    if os.path.isfile(path):
        if 'Range' in request.headers:
            start, end = get_range(request)
            res = partial_response(path, start, end)
        else:
            res = send_from_directory(path)
            res.headers.add('Content-Disposition', 'attachment')
    else:
        res = make_response('Not found', 404)

    return res


def allowed_file(filename):
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS


@app.route('/', methods = ['GET', 'POST'])
def index():
    return render_template('index.html')


@app.route('/upload', methods = ['GET'])
def upload_page():
    return render_template('upload.html')


@app.route('/uploader', methods = ['GET', 'POST'])
def uploader():
    if request.method == 'POST':
        file = request.files['file']
        if file and allowed_file(file.filename):
            file_name = secure_filename(file.filename)
            file_path = os.path.join(app.config['UPLOAD_FOLDER'], file_name)

            if os.path.exists(file_path):
                print('File is existed: {}'.format(file_path))
                os.remove(file_path)

            file.save(file_path)

            host_name = urlparse(request.base_url).hostname

            return 'upload success. <br/>GET url is: <h3>http://{}/upload/{}<h3>'.format(host_name, file_name)

    return render_template('upload.html')


@app.route('/upload/<file_name>', methods = ['GET'])
def get_uploaded_file(file_name):
    path = './upload/{}'.format(file_name)
    return download_file(path)

@app.route('/dfu_file_52', methods = ['GET'])
def get_dfu_file_52():
    path = './files/dfu_file_52.bin'
    return download_file(path)


@app.route('/dfu_file_91', methods = ['GET'])
def get_dfu_file_91():
    path = './files/dfu_file_91.bin'
    return download_file(path)

@app.route('/uploaded_files', methods = ['GET'])
def list_uploaded_files():
    if not os.path.exists(UPLOAD_FOLDER):
        return 'No file'

    file_list = '<h2>Uploaded file list</h2>'
    for _file in os.listdir(UPLOAD_FOLDER):
        if _file.endswith('.bin'):
            file_path = os.path.join(UPLOAD_FOLDER, _file)
            file_time_raw = os.path.getmtime(file_path)
            file_time = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(file_time_raw))

            file_list += '<p>File name: ' + _file + '</p>'
            file_list += '<p>Modify time: ' + file_time + '</p>'
            file_list += '<p>---</p>'

            # return file_path
    return file_list


if __name__ == '__main__':
    # Default port is 5000
    app.run(host='0.0.0.0')

