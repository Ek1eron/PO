from locust import HttpUser, task, between

class WebsiteUser(HttpUser):
    wait_time = between(0.01, 0.1)


    @task(2)
    def index_page(self):
        self.client.get("/")

    @task(1)
    def second_page(self):
        self.client.get("/page2.html")
