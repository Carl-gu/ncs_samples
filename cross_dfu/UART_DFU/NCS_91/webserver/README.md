## HOW TO DEPLOY 

Suppose you have a Linux server.



### Code

Push`www` folder to `/root` of your server.

The web server is developed by Flask framework in Python. 

### Gunicorn

[Gunicorn](https://gunicorn.org/), *Green Unicorn*, is a Python web server gateway interface (WSGI) HTTP Server for UNIX.  It is used to forward requests from the NGINX web server to the Flask  application.

Online install: `$ apt install gunicorn`

Run flask app: `$ gunicorn -w 3 nrf91_server:app`

### Supervisor

It's a process demon tool, with the help of this tool, Flask program can keep working even after SSH terminal is closed.

#### Install

`$ pip install supervisor`

#### Config

Generate a config file:`$ echo_supervisord_conf > /etc/supervisord.conf`, refer to this [official link](http://supervisord.org/installing.html#creating-a-configuration-file). 

At `/etc`, you can find a file named `supervisord.conf`.

Add a program by modifying supervisord.conf.

Read this [official doc](http://supervisord.org/running.html#adding-a-program) or refer my setting file(`.\config files\supervisord.conf`).

The key part is:

```
[program:nrf91_server]
directory=/root/www/nrf91
command=gunicorn --workers=3 nrf91_server:app
autostart=true
autorestart=true
stopasgroup=true
killasgroup=true
stderr_logfile=/var/log/nrf91_server/nrf91_server.err.log
stdout_logfile=/var/log/nrf91_server/nrf91_server.out.log
```

Refer: https://www.linode.com/docs/development/python/flask-and-gunicorn-on-ubuntu/

- 把tmp目录改到supervisor目录下
- 在该目录下新建sock等文件，并chmod 777设置权限
  - https://blog.csdn.net/qw1993422/article/details/77649425

#### Usage

- Reload: supervisorctl reload
- Restart: supervisorctl restart \<your-app> or all
- Start: supervisorctl start \<your-app> or all
- Stop: supervisorctl stop \<your-app> or all

### Nginx

[NGINX](https://www.linode.com/docs/web-servers/nginx/nginx-installation-and-basic-setup/) is open-source software that can be used as a high-performance web server, reverse proxy, load-balancer, and more.

#### Install

`$ apt install nginx`

#### Add a site

`$ vim /etc/nginx/sites-enabled/nrf91_server`

The content is:

```
server {
        listen 80;
        server_name localhost;

        location / {
                proxy_pass http://localhost:8000;
                proxy_set_header Host $host;
                proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        }
}
```

Or refer to my file(`.\config files\nrf91_server`).



(end)